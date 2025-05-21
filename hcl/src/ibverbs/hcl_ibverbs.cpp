#include "hcl_utils.h"
#include "hcl_ibverbs.h"
#include "hcl_types.h"                                    // for portMaskConfig
#include "platform/gen2_arch_common/types.h"              // for MAX_NICS_GEN2ARCH
#include "platform/gen2_arch_common/hcl_device_config.h"  // for HclDeviceConfig
#include "hcl_types.h"                                    // for eIbvNicPhysicalState

#include <string>
#include <dlfcn.h>
#include <poll.h>
#include "helpers.h"
#include <dirent.h>

// default lag size
#define DEFAULT_LAG_SIZE (3)

hcl_ibverbs_t g_ibv;

std::ostream& operator<<(std::ostream& os, const ibv_gid& gid)
{
    return os << "GID(0x" << std::hex << gid.global.interface_id << ", 0x" << gid.global.subnet_prefix << std::dec
              << ")";
}
HLLOG_DEFINE_OSTREAM_FORMATTER(ibv_gid);

uint64_t hcl_ibverbs_t::getHwIpPortMask(const int fd) const
{
    hlthunk_hw_ip_info hw_ip {};

    const int rc = hlthunk_get_hw_ip_info(fd, &hw_ip);
    if (rc)
    {
        ERR_IBV("failed reading hw_ip from fd {} with rc {} errno {} {}", fd, rc, errno, strerror(errno));
        return false;
    }

    return hw_ip.nic_ports_mask;
}

hcclResult_t hcl_ibverbs_t::init(const HclDeviceConfig& deviceConfig)
{
    LOG_IBV("Called for fd={}", deviceConfig.getFd());

    cqs_.resize(MAX_NICS_GEN2ARCH, nullptr);

    int fd = deviceConfig.getFd();

    if (!ibv_.load())
    {
        ERR_IBV("failed to load ibverbs interface. [{}]", dlerror());
        return hcclInternalError;
    }

    const int device_idx = deviceConfig.getDeviceIndex();

    /* Prepare IB device name using device index, for each hlX device there will be a hlib_X device */
    ib_devname_ = "hbl_" + std::to_string(device_idx);

    /* Get list of all IB devices and locate matching Habana IB device */
    ibv_device** dev_list;
    int          num_of_device;

    // Returns a NULL-terminated array of IB devices.
    dev_list = ibv_.ibv_get_device_list(&num_of_device);

    LOG_IBV("ibv_get_device_list() -> {}", num_of_device);

    std::string all_dev = "found devices: " + std::to_string(num_of_device);

    ibv_device* ibdev_ = nullptr;
    std::string devName;

    for (auto i = 0; dev_list[i]; ++i)
    {
        devName = ibv_.ibv_get_device_name(dev_list[i]);

        all_dev += " " + devName;

        LOG_IBV("ibv_get_device_name({}) == {}", i, devName);

        if (devName == ib_devname_)
        {
            /* found a matching Habana IB device for given Habana compute device*/
            ibdev_ = dev_list[i];
            break;
        }
    }

    ibv_.ibv_free_device_list(dev_list);

    if (ibdev_ == nullptr)
    {
        // (ibdev_ == nullptr) can be either a failure or single-device configuration.
        // To know, check (temp solution) if the port mask is 0
        uint64_t hw_ip_port_mask = getHwIpPortMask(fd);
        if (hw_ip_port_mask != 0)
        {
            ERR_IBV("failed to find matching Habana IB device ({}) [{}] nic_ports_mask {:x}",
                    ib_devname_,
                    all_dev,
                    hw_ip_port_mask);
            return hcclInternalError;
        }
        INF_IBV("No active ports for device {}", ib_devname_);
    }

    if (ibdev_)
    {
        hbldv_ucontext_attr attr = {};
        attr.core_fd             = (uint32_t)fd;

        ibctx_ = ibv_.hbldv_open_device(ibdev_, &attr);
        VERIFY(ibctx_, "hbldv_open_device({}(fd:{})), mask: 0x{:x}) failed.", ib_devname_, fd, attr.ports_mask);

        ibpd_ = ibv_.ibv_alloc_pd(ibctx_);
        VERIFY(ibpd_, "ibv_alloc_pd() failed.");
    }

    portMaskConfig mask;
    get_port_mask(mask);

    map_ib_ports(mask.hwPortsMask);

    dram_enabled_ = deviceConfig.getDramEnabled();

    parse_sysfs_infiniband();

    init_ = true;
    return hcclSuccess;
}

void hcl_ibverbs_t::set_hcl_device(IHclDevice* device)
{
    VERIFY(init_, "Cant set device w/o init called first");
    LOG_IBV("Setting device to {}", device->getDeviceTypeStr());
    device_ = device;
}

void hcl_ibverbs_t::map_ib_ports(const nics_mask_t nics_mask)
{
    LOG_IBV("0x{:x}", (uint64_t)nics_mask);
    nic2port_.resize(MAX_NICS_GEN2ARCH, -1);
    port2nic_.resize(MAX_NICS_GEN2ARCH + 1, -1);

    uint32_t ib_port = 1;
    FOR_I(MAX_NICS_GEN2ARCH)
    {
        if (!nics_mask[i]) continue;

        nic2port_[i]       = ib_port;
        port2nic_[ib_port] = i;

        LOG_IBV("hcl nic({}) -> ibv port({})", i, ib_port);
        ib_port++;
    }
}

void hcl_ibverbs_t::nic_mask_to_ib_port_mask(const nics_mask_t& nicsMask, bool isScaleout)
{
    nics_mask_t& ibPortsMask = isScaleout ? scaleout_ports_ : scaleup_ports_;
    ibPortsMask              = 0;

    for (auto nic : nicsMask)
    {
        ibPortsMask[nic2port_[nic]] = 1;
    }

    LOG_IBV("nicsMask=0x{:x}, {}=0x{:x}",
            (uint64_t)nicsMask,
            isScaleout ? "ibScaleoutPortsMask" : "ibScaleupPortsMask",
            (uint64_t)ibPortsMask);
}

#define _free_objs(objs, lambda)                                                                                       \
    std::for_each(objs.begin(), objs.end(), lambda);                                                                   \
    objs.clear()

void hcl_ibverbs_t::close()
{
    // ensure that eq_poll() function exited before closing
    if (!poll_done_)
    {
        *poll_stop_ = true;
        while (!poll_done_)
        {
            __builtin_ia32_pause();
        }
    }

    for (auto& [comm, qp_map] : qps_())
    {
        for (auto& [key, ibqp] : qp_map)
        {
            WRN_IBV("not destroyed qp: {}, nic: {}, comm: {}", ibvqp_key_t(key).qpn, ibvqp_key_t(key).nic, comm);
            ibv_.ibv_destroy_qp(ibqp);
        }
        qp_map.clear();
    }

    qps_.clear();

    _free_objs(cqs_, [&](auto& _cq) {
        if (_cq) ibv_.ibv_destroy_cq(_cq);
    });

    _free_objs(fifos_, [&](auto& _fifo) {
        if (_fifo) ibv_.hbldv_destroy_usr_fifo(_fifo);
    });

    sysfs_ports_.clear();

    if (ibpd_) ibv_.ibv_dealloc_pd(ibpd_);
    if (ibctx_) ibv_.ibv_close_device(ibctx_);

    ibpd_  = nullptr;
    ibctx_ = nullptr;
}

void hcl_ibverbs_t::on_comm_init(comm_t comm)
{
    VERIFY(!qps_.exists(comm), "comm {} already exists", comm);

    qps_.emplace(comm, ibvqp_map_t());
}

void hcl_ibverbs_t::on_comm_destroy(comm_t comm)
{
    for (auto& [key, ibqp] : qps_.at(comm))
    {
        WRN_IBV("not destroyed qp: {}, nic: {}, comm: {}", ibvqp_key_t(key).qpn, ibvqp_key_t(key).nic, comm);
        ibv_.ibv_destroy_qp(ibqp);
    }

    qps_.erase(comm);
}

void hcl_ibverbs_t::setup_nic(uint32_t nic, uint32_t num_wqes, uint32_t bp, eNicType nt)
{
    //  * hbldv_port_ex_attr - HL port extended attributes.
    //
    //  * @wq_arr_attr: Array of WQ-array attributes for each WQ-array type.
    //  * @qp_wq_bp_offs: Offsets in NIC memory to signal a back pressure.
    //  * @atomic_fna_fifo_offs: SRAM/DCCM addresses provided to the HW by the
    //      user when FnA completion is configured in the SRAM/DDCM.
    //  * @port_num: Port ID (should be 1-based).
    //  * @atomic_fna_mask_size: Completion address value mask.
    //  * @advanced: WQ should support advanced operations such as RDV, QMan, WTD, etc.

    LOG_IBV("nic: {}, num_wqes: {}, bp: 0x{:x}, nt: {}", nic, num_wqes, bp, nt);

    static std::map<eNicType, hbldv_wq_array_type> nicType2wqType = {
        {ntGeneric, HBLDV_WQ_ARRAY_TYPE_GENERIC},
        {ntCollective, HBLDV_WQ_ARRAY_TYPE_COLLECTIVE},
        {ntScaleOut, HBLDV_WQ_ARRAY_TYPE_SCALE_OUT_COLLECTIVE}};

    hbldv_port_ex_attr port_attr = {};

    port_attr.port_num = nic2port_[nic];
    port_attr.caps |= HBLDV_PORT_CAP_ADVANCED;
    port_attr.qp_wq_bp_offs[0] = bp;

    auto wqType = nicType2wqType[nt];

    port_attr.wq_arr_attr[wqType].mem_id                = dram_enabled_ ? HBLDV_MEM_DEVICE : HBLDV_MEM_HOST;
    port_attr.wq_arr_attr[wqType].max_num_of_wqes_in_wq = device_->getSenderWqeTableSize();
    port_attr.wq_arr_attr[wqType].swq_granularity       = HBLDV_SWQE_GRAN_32B;
    port_attr.wq_arr_attr[wqType].max_num_of_wqs        = num_wqes;

    if (nt == ntScaleOut)
    {
        port_attr.wq_arr_attr[HBLDV_WQ_ARRAY_TYPE_GENERIC].mem_id = dram_enabled_ ? HBLDV_MEM_DEVICE : HBLDV_MEM_HOST;
        port_attr.wq_arr_attr[HBLDV_WQ_ARRAY_TYPE_GENERIC].max_num_of_wqes_in_wq = device_->getSenderWqeTableSize();
        port_attr.wq_arr_attr[HBLDV_WQ_ARRAY_TYPE_GENERIC].swq_granularity       = HBLDV_SWQE_GRAN_32B;
        port_attr.wq_arr_attr[HBLDV_WQ_ARRAY_TYPE_GENERIC].max_num_of_wqs        = num_wqes;
    }
    int ret = ibv_.hbldv_set_port_ex(ibctx_, &port_attr);

    VERIFY(ret == 0, "hbldv_set_port_ex() failed: {}, nic: {}", ret, nic);
}

void hcl_ibverbs_t::create_cq(uint32_t nic, int num_cqes)
{
    VERIFY(cqs_[nic] == nullptr, "nic {} already set {}", nic, cqs_[nic]);

    hbldv_cq_attr cq_attr = {};
    cq_attr.port_num      = (uint8_t)nic2port_[nic];

    /* on success, create CQ API returns a handle, it needs to be stored for future reference during QP creation */
    auto ibvcq = ibv_.hbldv_create_cq(ibctx_, num_cqes, NULL, 0, &cq_attr);

    VERIFY(ibvcq, "hbldv_create_cq(num_cqes: {}) failed. nic: {}", num_cqes, nic);

    cqs_[nic] = ibvcq;
}

void hcl_ibverbs_t::get_port_mask(portMaskConfig& portsMasks)
{
    if (!has_ib_device())
    {
        portsMasks.hwPortsMask    = 0;
        portsMasks.hwExtPortsMask = 0;
        return;
    }

    hbldv_device_attr device_attr {};

    int rc = ibv_.hbldv_query_device(ibctx_, &device_attr);
    VERIFY(rc == 0, "hbldv_query_device() failed. rc: {}", rc);

    // mask out scaleup ports for HL3_RACK (P0)
    if ((HclConfigType)GCFG_BOX_TYPE_ID.value() == HL3_RACK)
    {
        device_attr.ext_ports_mask &= (((1 << 23) | (1 << 22) | (1 << 8)) << 1);  // 1-based
    }

    portsMasks.hwPortsMask = device_attr.hw_ports_mask;

    const nics_mask_t hw_nics_mask      = device_attr.hw_ports_mask;
    const nics_mask_t ib_ext_ports_mask = device_attr.ext_ports_mask;

    uint32_t ib_port          = 1;
    portsMasks.hwExtPortsMask = 0;
    for (auto nic : hw_nics_mask)
    {
        if (ib_ext_ports_mask[ib_port])
        {
            portsMasks.hwExtPortsMask |= (1 << nic);
        }

        ib_port++;
    }
}

uint32_t hcl_ibverbs_t::create_qp(comm_t comm, bool sender, uint32_t nic, uint32_t qpHint)
{
    LOG_IBV("comm: {}, for {}, nic: {}, hint: {}", comm, sender ? "SEND" : "RECV", nic, qpHint);

    ibv_cq*             ibv_cq       = cqs_[nic];
    ibv_qp_init_attr    qp_init_attr = {};
    hbldv_query_qp_attr dv_qp_attr   = {};
    ibv_qp_attr         qp_attr      = {};
    hbldv_qp_attr       hl_qp_attr   = {};

    /* Set requestor and responder CQ. User may use same CQ for both. */
    qp_init_attr.send_cq          = ibv_cq;
    qp_init_attr.recv_cq          = ibv_cq;
    qp_init_attr.cap.max_recv_wr  = 1;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    /* Reliable connection. */
    qp_init_attr.qp_type = IBV_QPT_RC;

    /* req WQ size */
    if (sender)
    {
        qp_init_attr.cap.max_send_wr = device_->getSenderWqeTableSize();
    }
    else
    {
        qp_init_attr.cap.max_send_wr = device_->getReceiverWqeTableSize();
    }

    /* 1. Create QP in RESET state. */
    ibv_qp* ibqp = ibv_.ibv_create_qp(ibpd_, &qp_init_attr);
    VERIFY(ibqp, "ibv_create_qp() failed, nic: {}", nic);

    int rc = ibv_.ibv_query_qp(ibqp, &qp_attr, IBV_QP_CAP, &qp_init_attr);
    VERIFY(rc == 0, "ibv_query_qp() failed: {}, nic: {}", rc, nic);

    /* 2. Transition QP from RESET to INIT state. */
    qp_attr.qp_state        = IBV_QPS_INIT;
    qp_attr.port_num        = nic2port_[nic];
    qp_attr.pkey_index      = 0;
    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;

    hl_qp_attr.wq_type = sender ? HBLDV_WQ_SEND_RDV : HBLDV_WQ_RECV_RDV;

    if (GCFG_HCL_USE_NIC_COMPRESSION.value())
    {
        hl_qp_attr.caps |= HBLDV_QP_CAP_COMPRESSION;
    }

    if (qpHint != 0)
    {
        hl_qp_attr.caps |= HBLDV_QP_CAP_COLL;
        hl_qp_attr.qp_num_hint = qpHint;
    }

    int attr_mask = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;

    rc = ibv_.hbldv_modify_qp(ibqp, &qp_attr, attr_mask, &hl_qp_attr);
    VERIFY(rc == 0, "hbldv_modify_qp(INIT) failed: {}, nic: {}", rc, nic);

    /* retrieve HL QP number using IB QP handle */
    /* it can be read from dv_qp_attr.qp_num */
    rc = ibv_.hbldv_query_qp(ibqp, &dv_qp_attr);
    VERIFY(rc == 0, "hbldv_query_qp() failed: {}, nic: {}", rc, nic);

    qps_.at(comm).emplace(nic, dv_qp_attr.qp_num, ibqp);
    LOG_IBV("--> {}", dv_qp_attr.qp_num);

    return dv_qp_attr.qp_num;
}

uint32_t hcl_ibverbs_t::create_migration_qp(comm_t   comm,
                                            bool     sender,
                                            uint32_t nic,
                                            uint32_t migratedNic,
                                            uint32_t migratedQpn,
                                            uint32_t qpHint)
{
    auto& qps = qps_.at(comm);

    ibv_qp*             migrated_qp  = qps(migratedNic, migratedQpn);
    ibv_cq*             ibv_cq       = cqs_[nic];
    ibv_qp_init_attr    qp_init_attr = {};
    hbldv_query_qp_attr dv_qp_attr   = {};
    ibv_qp_attr         qp_attr      = {};
    hbldv_qp_attr       hl_qp_attr   = {};

    LOG_IBV("for {}, nic: {}, migrated nic {}, migrated qpn {}, hint: {}",
            sender ? "SEND" : "RECV",
            nic,
            migratedNic,
            migratedQpn,
            qpHint);

    /* Set requestor and responder CQ. User may use same CQ for both. */
    qp_init_attr.send_cq          = ibv_cq;
    qp_init_attr.recv_cq          = ibv_cq;
    qp_init_attr.cap.max_recv_wr  = 1;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    /* Reliable connection. */
    qp_init_attr.qp_type = IBV_QPT_RC;

    /* req WQ size */
    if (sender)
    {
        qp_init_attr.cap.max_send_wr = device_->getSenderWqeTableSize();
    }
    else
    {
        qp_init_attr.cap.max_send_wr = device_->getReceiverWqeTableSize();
    }

    /* 1. Create QP in RESET state. */
    ibv_qp* ibqp = ibv_.ibv_create_qp(ibpd_, &qp_init_attr);
    VERIFY(ibqp, "ibv_create_qp() failed, nic: {}", nic);

    int rc = ibv_.ibv_query_qp(migrated_qp, &qp_attr, IBV_QP_CAP, &qp_init_attr);
    VERIFY(rc == 0, "ibv_query_qp() failed: {}, nic: {}", rc, nic);

    /* 2. Transition QP from RESET to INIT state. */
    qp_attr.qp_state        = IBV_QPS_INIT;
    qp_attr.port_num        = nic2port_[nic];
    qp_attr.pkey_index      = 0;
    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;

    hl_qp_attr.caps &= ~HBLDV_QP_CAP_COLL;  // clear collective bit
    hl_qp_attr.wq_type = sender ? HBLDV_WQ_SEND_RDV : HBLDV_WQ_RECV_RDV;

    if (GCFG_HCL_USE_NIC_COMPRESSION.value())
    {
        hl_qp_attr.caps |= HBLDV_QP_CAP_COMPRESSION;
    }

    int attr_mask = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;

    rc = ibv_.hbldv_modify_qp(ibqp, &qp_attr, attr_mask, &hl_qp_attr);
    VERIFY(rc == 0, "hbldv_modify_qp(INIT) failed: {}, nic: {}", rc, nic);

    /* retrieve HL QP number using IB QP handle */
    /* it can be read from dv_qp_attr.qp_num */
    rc = ibv_.hbldv_query_qp(ibqp, &dv_qp_attr);
    VERIFY(rc == 0, "hbldv_query_qp() failed: {}, nic: {}", rc, nic);

    qps.emplace(nic, dv_qp_attr.qp_num, ibqp);

    LOG_IBV("--> {}", dv_qp_attr.qp_num);

    return dv_qp_attr.qp_num;
}

void hcl_ibverbs_t::set_migration_qp_rtr(comm_t   comm,
                                         uint32_t nic,
                                         uint32_t qpn,
                                         uint32_t migratedNic,
                                         uint32_t migratedQpn,
                                         uint32_t src_ip,
                                         uint64_t src_mac,
                                         uint32_t dst_ip,
                                         uint64_t dst_mac,
                                         uint32_t dst_qp)
{
    LOG_IBV("comm: {}, for nic: {}, qpn {}, migrated nic {}, migrated qpn {}",
            comm,
            nic,
            qpn,
            migratedNic,
            migratedQpn);

    const auto& qps = qps_.at(comm);

    ibv_qp_attr   qp_attr    = {};
    hbldv_qp_attr hl_qp_attr = {};

    ibv_qp* migrated_qp = qps(migratedNic, migratedQpn);
    ibv_qp* ibqp        = qps(nic, qpn);

    /* 2. Transition QP from RESET to INIT state. */
    qp_attr.qp_state              = IBV_QPS_RTR;
    qp_attr.dest_qp_num           = dst_qp;
    qp_attr.port_num              = nic2port_[nic];
    qp_attr.ah_attr.is_global     = 1;
    qp_attr.ah_attr.grh.hop_limit = 0xff;
    qp_attr.ah_attr.port_num      = nic2port_[nic];

    qp_attr.ah_attr.grh.dgid       = dgid(dst_ip, dst_mac);
    qp_attr.ah_attr.grh.sgid_index = sgid_index(dst_ip, src_ip, src_mac, nic);

    hl_qp_attr.caps |= HBLDV_QP_CAP_MIGRATE;
    hl_qp_attr.qp_to_migrate = migrated_qp;

    const int attr_mask = IBV_QP_STATE | IBV_QP_AV | IBV_QP_DEST_QPN;

    int rc = ibv_.hbldv_modify_qp(ibqp, &qp_attr, attr_mask, &hl_qp_attr);
    if (rc != 0)
    {
        LOG_IBV("ibv_modify_qp failed: {}, nic: {} {} {}", rc, nic, errno, strerror(errno));
    }
    VERIFY(rc == 0, "hbldv_modify_qp(RTR) failed: {}, nic: {}", rc, nic);
}

void hcl_ibverbs_t::set_migration_qp_rts(comm_t   comm,
                                         uint32_t nic,
                                         uint32_t qpn,
                                         uint32_t migratedNic,
                                         uint32_t migratedQpn)
{
    LOG_IBV("comm: {}, for nic: {}, qpn {}, migrated nic {}, migrated qpn {}",
            comm,
            nic,
            qpn,
            migratedNic,
            migratedQpn);

    ibv_qp_attr   qp_attr    = {};
    hbldv_qp_attr hl_qp_attr = {};

    const auto& qps = qps_.at(comm);

    ibv_qp* migrated_qp = qps(migratedNic, migratedQpn);
    ibv_qp* ibqp        = qps(nic, qpn);

    /* 2. Transition QP from RESET to INIT state. */
    qp_attr.qp_state         = IBV_QPS_RTS;
    hl_qp_attr.qp_to_migrate = migrated_qp;
    hl_qp_attr.caps |= HBLDV_QP_CAP_MIGRATE;

    int rc = ibv_.hbldv_modify_qp(ibqp, &qp_attr, IBV_QP_STATE, &hl_qp_attr);
    VERIFY(rc == 0, "hbldv_modify_qp(RTS) failed: {}, nic: {}", rc, nic);
}

void hcl_ibverbs_t::migrate_qp(comm_t comm, uint32_t nic, uint32_t qpn)
{
    const auto& qps = qps_.at(comm);

    ibv_qp* ibqp = qps(nic, qpn);
    int     rc   = ibv_.hbldv_migrate_qp(ibqp);
    VERIFY(rc == 0, "hbldv_migrate_qp failed: {}, nic: {} qpn {}", rc, nic, qpn);
}

uint32_t hcl_ibverbs_t::reserve_collective_qp(bool is_scale_out)
{
    uint64_t           ports_mask   = is_scale_out ? scaleout_ports_ : scaleup_ports_;
    hbldv_coll_qp_attr coll_qp_attr = {.is_scale_out = is_scale_out, .ports_mask = ports_mask};
    hbldv_coll_qp      coll_qp      = {};

    int        rc        = EBUSY;
    const auto sleepTime = std::chrono::milliseconds(1000);
    auto       cnt       = std::chrono::seconds(GCFG_HCL_IBV_RETRY_TIMEOUT_SEC.value()) / sleepTime;
    while ((rc == EBUSY) && (cnt-- > 0))
    {
        rc = ibv_.hbldv_reserve_coll_qps(ibpd_, &coll_qp_attr, &coll_qp);
        if (rc == 0) break;
        LOG_IBV("reserve_collective_qp() is busy. cnt {}", cnt);
        std::this_thread::sleep_for(sleepTime);
    }
    VERIFY(rc == 0,
           "hbldv_reserve_coll_qps() failed: {} is_scale_out {} errno {} {}",
           rc,
           is_scale_out,
           errno,
           errno ? strerror(errno) : "");
    LOG_IBV("--> {} {}", coll_qp.qp_num, is_scale_out ? "(scale-out)" : "");
    return coll_qp.qp_num;
}

inline bool operator==(const ibv_gid& lhs, const ibv_gid& rhs)
{
    return memcmp(&lhs, &rhs, sizeof(ibv_gid)) == 0;
}

int hcl_ibverbs_t::sgid_index(uint32_t dst_ip, uint32_t src_ip, uint64_t src_mac, uint32_t nic)
{
    ibv_gid_type_sysfs src_type;
    ibv_gid            src_gid = {};

    int sgid_idx = -1;

    if (dst_ip == 0)
    {
        if (src_mac == 0xffffffffffff) return 0;  // internal ports

        mac_to_gid(src_mac, src_gid);
        src_type = IBV_GID_TYPE_SYSFS_IB_ROCE_V1;
    }
    else
    {
        ip4addr_to_gid(src_ip, src_gid);
        src_type = IBV_GID_TYPE_SYSFS_ROCE_V2;
    }

    if (GCFG_HCL_IBV_GID_SYSFS.value())
    {
        for (auto& sfs : sysfs_ports_[nic2port_[nic]])
        {
            if ((src_gid == sfs.second.gid) && (src_type == sfs.second.type))
            {
                sgid_idx = sfs.first;
                break;
            }
        }
    }
    else
    {
        ibv_port_attr port_attr = {};

        int rc = ibv_.ibv_query_port(ibctx_, nic2port_[nic], &port_attr);
        VERIFY(rc == 0, "ibv_query_port({}) failed ({})", nic2port_[nic], rc);

        for (int i = 0; i < port_attr.gid_tbl_len; i++)
        {
            ibv_gid tmp_gid = {};

            rc = ibv_.ibv_query_gid(ibctx_, nic2port_[nic], i, &tmp_gid);
            VERIFY(rc == 0, "ibv_query_gid({}) failed ({}). nic:{}", i, rc, nic);

            LOG_IBV("port:{} [{}] -> tmp_gid: {}", nic2port_[nic], i, tmp_gid);

            if (src_gid == tmp_gid)
            {
                ibv_gid_type_sysfs type;
                rc = ibv_.ibv_query_gid_type(ibctx_, nic2port_[nic], i, &type);
                VERIFY(rc == 0, "ibv_query_gid_type({}) failed ({}). nic:{}", i, rc, nic);
                if (type == src_type)
                {
                    sgid_idx = i;
                    break;
                }
            }
        }
    }

    LOG_IBV("src_gid: {}, type: {} -> {}", src_gid, src_type, sgid_idx);

    VERIFY(sgid_idx != -1, "source GID not found. nic: {} ip: {}, mac: 0x{:x}", nic, ip2str(src_ip), src_mac);

    return sgid_idx;
}

ibv_gid hcl_ibverbs_t::dgid(uint32_t dst_ip, uint64_t dst_mac)
{
    ibv_gid gid = {};

    if (dst_ip)
    {
        ip4addr_to_gid(dst_ip, gid);
    }
    else
    {
        mac_to_gid(dst_mac, gid);
    }

    return gid;
}

void hcl_ibverbs_t::set_qp_ctx(comm_t   comm,
                               uint32_t qpn,
                               uint32_t nic,
                               uint32_t src_ip,
                               uint64_t src_mac,
                               uint32_t dst_ip,
                               uint64_t dst_mac,
                               uint32_t dst_qp,
                               uint8_t  lagIdx,
                               uint8_t  lastInLag)
{
    LOG_IBV("comm:{}, qp:{}->{} nic:{} ip:{} -> {} mac: 0x{:x} -> 0x{:x}",
            comm,
            qpn,
            dst_qp,
            nic,
            ip2str(src_ip),
            ip2str(dst_ip),
            src_mac,
            dst_mac);

    ibv_qp_attr   qp_attr    = {};
    hbldv_qp_attr hl_qp_attr = {};

    ibv_qp* ibv_qp = qps_.at(comm).at(nic, qpn);

    /* Initialize the generic IBV QP params */
    qp_attr.qp_state           = IBV_QPS_RTR;  // Responder
    qp_attr.path_mtu           = to_ibdev_mtu(GCFG_MTU_SIZE.value());
    qp_attr.dest_qp_num        = dst_qp;
    qp_attr.max_dest_rd_atomic = 1;

    qp_attr.ah_attr.port_num      = nic2port_[nic];
    qp_attr.ah_attr.is_global     = 1;
    qp_attr.ah_attr.grh.hop_limit = 0xFF;

    qp_attr.ah_attr.grh.dgid       = dgid(dst_ip, dst_mac);
    qp_attr.ah_attr.grh.sgid_index = sgid_index(dst_ip, src_ip, src_mac, nic);

    /* set the attribute mask with the specific fields we request to change */
    int attr_mask = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                    IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;

    /* Now, initialize the specific habana QP params */
    hl_qp_attr.wq_granularity = HBLDV_SWQE_GRAN_32B;

    int rc = ibv_.hbldv_modify_qp(ibv_qp, &qp_attr, attr_mask, &hl_qp_attr);
    VERIFY(rc == 0, "hbldv_modify_qp(RTR) failed ({}). nic:{}, qp:{}", rc, nic, qpn);

    //-----------------------------------------------------------------------------------------------------------------------------

    qp_attr.qp_state      = IBV_QPS_RTS;  // Requester
    qp_attr.timeout       = 13;
    qp_attr.retry_cnt     = 7;
    qp_attr.rnr_retry     = 7;
    qp_attr.sq_psn        = 0;
    qp_attr.max_rd_atomic = 1;

    /* set the attribute mask with the specific fields we request to change */
    attr_mask =
        IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;

    /* Append the requester relevant params into the hl-attr */
    hl_qp_attr.dest_wq_size   = device_->getSenderWqeTableSize();
    hl_qp_attr.priority       = GCFG_REQUESTER_PRIORITY.value();
    hl_qp_attr.congestion_wnd = GCFG_CONGESTION_WINDOW.value();
    hl_qp_attr.caps |= GCFG_CONGESTION_CONTROL_ENABLE.value() ? HBLDV_QP_CAP_CONG_CTRL : 0;
    if (GCFG_HCL_USE_NIC_COMPRESSION.value())
    {
        hl_qp_attr.caps |= HBLDV_QP_CAP_COMPRESSION;
    }
    hl_qp_attr.coll_lag_idx     = lagIdx;
    hl_qp_attr.coll_last_in_lag = lastInLag;

    // config lag_size
    hl_qp_attr.coll_lag_size = DEFAULT_LAG_SIZE;

    rc = ibv_.hbldv_modify_qp(ibv_qp, &qp_attr, attr_mask, &hl_qp_attr);
    VERIFY(rc == 0, "hbldv_modify_qp(RTS) failed ({}) nic:{}, qp:{}", rc, nic, qpn);
}

void hcl_ibverbs_t::destroy_qp(comm_t comm, uint32_t nic, uint32_t qpn)
{
    LOG_IBV("comm: {}, nic: {} qp: {}", comm, nic, qpn);

    ibv_.ibv_destroy_qp(qps_.at(comm).at(nic, qpn));
    qps_.at(comm).erase(nic, qpn);
}

void hcl_ibverbs_t::create_fifos(scal_handle_t scal_handle)
{
    unsigned nicUserDbFifoParamsCount = 0;
    int      rc = scal_nics_db_fifos_init_and_allocV2(scal_handle, nullptr, nullptr, &nicUserDbFifoParamsCount);
    VERIFY(rc == 0, "scal_nics_db_fifos_init_and_alloc(h, 0 , 0, &cnt) failed: {}", rc);

    fifos_.resize(nicUserDbFifoParamsCount);

    scal_ibverbs_init_params initParams;
    initParams.ibv_ctxt         = ibctx_;
    initParams.ibverbsLibHandle = ibv_.lib_handle();
    initParams.nicsMask         = device_->getNicsStatusMask();

    LOG_IBV("initParams.nicsMask: {:x}", initParams.nicsMask);
    scal_nics_db_fifos_init_and_allocV2(scal_handle, &initParams, fifos_.data(), &nicUserDbFifoParamsCount);
    VERIFY(rc == 0,
           "scal_nics_db_fifos_init_and_alloc failed: {}, nicMask: {:x}, fifoCnt: {}",
           rc,
           initParams.nicsMask,
           nicUserDbFifoParamsCount);
}

bool hcl_ibverbs_t::is_nic_up(uint32_t nic)
{
    if (!has_ib_device()) return false;

    ibv_port_attr port_attr = {};

#if defined(ibv_query_port)
#undef ibv_query_port
#endif

    int rc = ibv_.ibv_query_port(ibctx_, nic2port_[nic], &port_attr);

    return (rc == 0) && (port_attr.state == IBV_PORT_ACTIVE);
}

const eIbvNicPhysicalState hcl_ibverbs_t::get_nic_phys_state(const uint32_t nic)
{
    if (!has_ib_device()) return eIbvNicPhysicalState::Undefined;

    ibv_port_attr port_attr = {};

    const int rc = ibv_.ibv_query_port(ibctx_, nic2port_[nic], &port_attr);

    LOG_IBV("nic: {} rc {}, phys_state: {}", nic, rc, port_attr.phys_state);

    if (rc != 0)
    {
        ERR_IBV("failed reading nic {} physical status with rc {} errno {} {}", nic, rc, errno, strerror(errno));
        return eIbvNicPhysicalState::Undefined;
    }

    switch (port_attr.phys_state)
    {
        case 3:  // IB_PORT_PHYS_STATE_DISABLED
            return eIbvNicPhysicalState::Shutdown;
        case 5:  // IB_PORT_PHYS_STATE_LINKUP
            return eIbvNicPhysicalState::LinkUp;
        default:
            return eIbvNicPhysicalState::Undefined;
    }
}

uint32_t hcl_ibverbs_t::get_qp_offset(uint32_t nic)
{
    hbldv_query_port_attr query_port_attr = {};

    int rc = ibv_.hbldv_query_port(ibctx_, nic2port_[nic], &query_port_attr);
    VERIFY(rc == 0, "hbldv_query_port() failed: {}, nic: {}", rc, nic);

    LOG_IBV("nic: {} offs: {}", nic, query_port_attr.coll_qps_offset);

    return query_port_attr.coll_qps_offset;
}

void hcl_ibverbs_t::parse_sysfs_infiniband()
{
    if (!GCFG_HCL_IBV_GID_SYSFS.value())
    {
        return;
    }

    sysfs_ports_.clear();
    walk_fs("/sys/class/infiniband/" + ib_devname_ + "/ports");

    if (LOG_LEVEL_AT_LEAST_TRACE(HCL))
    {
        for (auto& pr_port : sysfs_ports_)
        {
            for (auto& pr_gid : pr_port.second)
            {
                LOG_IBV("sysfs_ports[{}][{}] == {}, {}",
                        pr_port.first,
                        pr_gid.first,
                        pr_gid.second.gid,
                        pr_gid.second.type);
            }
        }
    }
}

void hcl_ibverbs_t::walk_fs(const std::string& path, uint32_t port)
{
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr)
    {
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (entry->d_type == DT_DIR)
        {
            if ((std::string(entry->d_name) != ".") && (std::string(entry->d_name) != ".."))
            {
                std::string subdirPath = path + "/" + entry->d_name;
                walk_fs(subdirPath, port == 0 ? atoi(entry->d_name) : port);
            }
        }
        else
        {
            std::string full_name = path + "/" + entry->d_name;
            auto        index     = atoi(entry->d_name);

            if (full_name.find("/gids/") != std::string::npos)
            {
                sysfs_ports_[port][index].gid = handle_gid(full_name);
            }
            else if (full_name.find("/gid_attrs/types/") != std::string::npos)
            {
                sysfs_ports_[port][index].type = handle_gid_type(full_name);
            }
        }
    }

    closedir(dir);
}
