#include "hcl_utils.h"
#include "hcl_ibverbs.h"
#include "hlthunk.h"
#include <string>
#include <dlfcn.h>
#include "interfaces/hcl_idevice.h"
#include <poll.h>
#include "helpers.h"
#include <dirent.h>

#define LOG_IBV(...) LOG_HCL_TRACE(HCL_IBV, ##__VA_ARGS__)
#define INF_IBV(...) LOG_HCL_INFO(HCL_IBV, ##__VA_ARGS__)
#define ERR_IBV(...) LOG_HCL_ERR(HCL_IBV, ##__VA_ARGS__)
#define WRN_IBV(...) LOG_HCL_WARN(HCL_IBV, ##__VA_ARGS__)
#define CRT_IBV(...) LOG_HCL_CRITICAL(HCL_IBV, ##__VA_ARGS__)

hcl_ibverbs_t g_ibv;

std::ostream& operator<<(std::ostream& os, const ibv_gid& gid)
{
    return os << "GID(0x" << std::hex << gid.global.interface_id << ", 0x" << gid.global.subnet_prefix << std::dec << ")";
}

hcclResult_t hcl_ibverbs_t::init(IHclDevice* device)
{
    device_ = device;

    cqs_.resize(device_->getHal()->getMaxNics(), nullptr);

    int fd = device_->getFd();

    if (!ibv_.load())
    {
        ERR_IBV("failed to load ibverbs interface. [{}]", dlerror());
        return hcclInternalError;
    }

#define PCI_ID_STR_LEN 13

    char pci_bus_id[PCI_ID_STR_LEN];
    int rc = hlthunk_get_pci_bus_id_from_fd(fd, pci_bus_id, sizeof(pci_bus_id));
    VERIFY(rc == 0, "hlthunk_get_pci_bus_id_from_fd() failed: {}", rc);

    /* Get device index from bus ID */
    int device_idx = hlthunk_get_device_index_from_pci_bus_id(pci_bus_id);

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
        ERR_IBV("failed to find matching Habana IB device ({}) [{}]", ib_devname_, all_dev);
        return hcclInternalError;
    }

    hlthunk_nic_get_ports_masks_out mask = {};
    int ret  = hlthunk_nic_get_ports_masks(fd, &mask);
    VERIFY(ret == 0, "hlthunk_nic_get_ports_masks() failed: {}", ret);

    map_ib_ports(mask.ports_mask);

    hbldv_ucontext_attr attr = {};
    attr.core_fd = (uint32_t)fd;

    ibctx_ = ibv_.hbldv_open_device(ibdev_, &attr);
    VERIFY(ibctx_, "hbldv_open_device({}(fd:{})), mask: 0x{:x}) failed.", ib_devname_, fd, attr.ports_mask);

    ibpd_ = ibv_.ibv_alloc_pd(ibctx_);
    VERIFY(ibpd_, "ibv_alloc_pd() failed.");

    struct hlthunk_hw_ip_info hw_ip = {};
    hlthunk_get_hw_ip_info(fd, &hw_ip);
    dram_enabled_ = hw_ip.dram_enabled;

    parse_sysfs_infiniband();

    return hcclSuccess;
}

void hcl_ibverbs_t::map_ib_ports(const nics_mask_t nics_mask)
{
    LOG_IBV("0x{:x}", (uint64_t)nics_mask);
    nic2port_.resize(device_->getHal()->getMaxNics(), -1);
    port2nic_.resize(device_->getHal()->getMaxNics() + 1, -1);

    uint32_t ib_port = 1;
    FOR_I(device_->getHal()->getMaxNics())
    {
        if (!nics_mask[i]) continue;

        nic2port_[i]       = ib_port;
        port2nic_[ib_port] = i;

        LOG_IBV("hcl nic({}) -> ibv port({})", i, ib_port);
        ib_port++;
    }
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

    _free_objs(qps_, [&](auto& _pair) { ibv_.ibv_destroy_qp(_pair.second); });
    _free_objs(cqs_, [&](auto& _cq) { if (_cq) ibv_.ibv_destroy_cq(_cq); });
    _free_objs(fifos_, [&](auto& _fifo) { if (_fifo) ibv_.hbldv_destroy_usr_fifo(_fifo); });

    sysfs_ports_.clear();

    if (ibpd_) ibv_.ibv_dealloc_pd(ibpd_);
    if (ibctx_) ibv_.ibv_close_device(ibctx_);

    ibpd_  = nullptr;
    ibctx_ = nullptr;
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

    static std::map<eNicType, hbldv_wq_array_type> nicType2wqType = {{ntGeneric, HBLDV_WQ_ARRAY_TYPE_GENERIC},
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

    VERIFY(ibvcq, "hbldv_create_cq(num_cqes: {}) failed. nic: {}", nic, num_cqes);

    cqs_[nic] = ibvcq;
}

uint32_t hcl_ibverbs_t::create_qp(bool sender, uint32_t nic, uint32_t qpHint)
{
    LOG_IBV("for {}, nic: {}, hint: {}", sender ? "SEND" : "RECV", nic, qpHint);

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

    qps_.emplace(nic, dv_qp_attr.qp_num, ibqp);
    LOG_IBV("--> {}", dv_qp_attr.qp_num);

    return dv_qp_attr.qp_num;
}

uint32_t hcl_ibverbs_t::create_collective_qp(bool is_scale_out)
{
    hbldv_coll_qp_attr coll_qp_attr = {.is_scale_out = is_scale_out};
    hbldv_coll_qp      coll_qp      = {};

    int rc = ibv_.hbldv_reserve_coll_qps(ibpd_, &coll_qp_attr, &coll_qp);
    VERIFY(rc == 0, "hbldv_reserve_coll_qps() failed: {}", rc);
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

void hcl_ibverbs_t::set_qp_ctx(uint32_t qpn,
                               uint32_t nic,
                               uint32_t src_ip,
                               uint64_t src_mac,
                               uint32_t dst_ip,
                               uint64_t dst_mac,
                               uint32_t dst_qp,
                               uint8_t  lagIdx,
                               uint8_t  lastInLag)
{
    LOG_IBV("qp:{}->{} nic:{} ip:{} -> {} mac: 0x{:x} -> 0x{:x}",
            qpn,
            dst_qp,
            nic,
            ip2str(src_ip),
            ip2str(dst_ip),
            src_mac,
            dst_mac);

    ibv_qp_attr       qp_attr    = {};
    hbldv_qp_attr     hl_qp_attr = {};
    ibv_qp*           ibv_qp     = qps_(nic, qpn);

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
    hl_qp_attr.dest_wq_size     = device_->getSenderWqeTableSize();
    hl_qp_attr.priority         = GCFG_REQUESTER_PRIORITY.value();
    hl_qp_attr.congestion_wnd   = GCFG_CONGESTION_WINDOW.value();
    hl_qp_attr.caps            |= GCFG_CONGESTION_CONTROL_ENABLE.value() ? HBLDV_QP_CAP_CONG_CTRL : 0;
    hl_qp_attr.coll_lag_idx     = lagIdx;
    hl_qp_attr.coll_last_in_lag = lastInLag;

    rc = ibv_.hbldv_modify_qp(ibv_qp, &qp_attr, attr_mask, &hl_qp_attr);
    VERIFY(rc == 0, "hbldv_modify_qp(RTS) failed ({}) nic:{}, qp:{}", rc, nic, qpn);
}

void hcl_ibverbs_t::destroy_qp(uint32_t nic, uint32_t qpn)
{
    LOG_IBV("nic: {} qp: {}", nic, qpn);
    ibv_.ibv_destroy_qp(qps_(nic, qpn));
    qps_.erase(nic, qpn);
}

void hcl_ibverbs_t::create_fifos(scal_handle_t scal_handle)
{
    unsigned nicUserDbFifoParamsCount = 0;
    int rc = scal_nics_db_fifos_init_and_allocV2(scal_handle, nullptr, nullptr, &nicUserDbFifoParamsCount);
    VERIFY(rc == 0, "scal_nics_db_fifos_init_and_allocV2(h, 0 , 0, &cnt) failed: {}", rc);

    fifos_.resize(nicUserDbFifoParamsCount);

    scal_ibverbs_init_params initParams;
    initParams.ibv_ctxt         = ibctx_;
    initParams.ibverbsLibHandle = ibv_.lib_handle();
    initParams.nicsMask         = device_->getNicsStatusMask();

    scal_nics_db_fifos_init_and_allocV2(scal_handle, &initParams, fifos_.data(), &nicUserDbFifoParamsCount);
    VERIFY(rc == 0,
           "scal_nics_db_fifos_init_and_allocV2 failed: {}, nicMask: {:x}, fifoCnt: {}",
           rc,
           initParams.nicsMask,
           nicUserDbFifoParamsCount);
}

bool hcl_ibverbs_t::is_nic_up(uint32_t nic)
{
    ibv_port_attr port_attr = {};

#if defined(ibv_query_port)
#undef ibv_query_port
#endif

    int rc = ibv_.ibv_query_port(ibctx_, nic2port_[nic], &port_attr);

    return (rc == 0) && (port_attr.state == IBV_PORT_ACTIVE);
}

uint32_t hcl_ibverbs_t::get_qp_offset(uint32_t nic)
{
    hbldv_query_port_attr query_port_attr = {};

    int rc = ibv_.hbldv_query_port(ibctx_, nic2port_[nic], &query_port_attr);
    VERIFY(rc == 0, "hbldv_query_port() failed: {}, nic: {}", rc, nic);

    LOG_IBV("nic: {} offs: {}", nic, query_port_attr.coll_qps_offset);

    return query_port_attr.coll_qps_offset;
}

// ===============================================================

// non-trivial designated initializers not supported
#define MAX_ERRORS (IBV_EVENT_WQ_FATAL + 1)
static const char* err2str[MAX_ERRORS] = {};

#define MAX_SYNDROMS 0x100
static const char* qp_syndroms[MAX_SYNDROMS] = {};

void init_error_tables()
{
    err2str[IBV_EVENT_QP_FATAL]            = "QP fatal event";
    err2str[IBV_EVENT_QP_REQ_ERR]          = "QP requester error";
    err2str[IBV_EVENT_QP_LAST_WQE_REACHED] = "QP align counters";
    err2str[IBV_EVENT_PATH_MIG]            = "WTD security error";
    err2str[IBV_EVENT_PATH_MIG_ERR]        = "Numerical error";
    err2str[IBV_EVENT_PORT_ACTIVE]         = "Link up";
    err2str[IBV_EVENT_PORT_ERR]            = "Link down";
    err2str[IBV_EVENT_GID_CHANGE]          = "GID table change",

    /* Rx packet errors*/
    qp_syndroms[0x1]  = "[RX] pkt err, pkt bad format";
    qp_syndroms[0x2]  = "[RX] pkt err, pkt tunnel invalid";
    qp_syndroms[0x3]  = "[RX] pkt err, BTH opcode invalid";
    qp_syndroms[0x4]  = "[RX] pkt err, syndrome invalid";
    qp_syndroms[0x5]  = "[RX] pkt err, Reliable QP max size invalid";
    qp_syndroms[0x6]  = "[RX] pkt err, Reliable QP min size invalid";
    qp_syndroms[0x7]  = "[RX] pkt err, Raw min size invalid";
    qp_syndroms[0x8]  = "[RX] pkt err, Raw max size invalid";
    qp_syndroms[0x9]  = "[RX] pkt err, QP invalid";
    qp_syndroms[0xa]  = "[RX] pkt err, Transport Service mismatch";
    qp_syndroms[0xb]  = "[RX] pkt err, QPC Requester QP state invalid";
    qp_syndroms[0xc]  = "[RX] pkt err, QPC Responder QP state invalid";
    qp_syndroms[0xd]  = "[RX] pkt err, QPC Responder resync invalid";
    qp_syndroms[0xe]  = "[RX] pkt err, QPC Requester PSN invalid";
    qp_syndroms[0xf]  = "[RX] pkt err, QPC Requester PSN unset";
    qp_syndroms[0x10] = "[RX] pkt err, QPC Responder RKEY invalid";
    qp_syndroms[0x11] = "[RX] pkt err, WQE index mismatch";
    qp_syndroms[0x12] = "[RX] pkt err, WQE write opcode invalid";
    qp_syndroms[0x13] = "[RX] pkt err, WQE Rendezvous opcode invalid";
    qp_syndroms[0x14] = "[RX] pkt err, WQE Read  opcode invalid";
    qp_syndroms[0x15] = "[RX] pkt err, WQE Write Zero";
    qp_syndroms[0x16] = "[RX] pkt err, WQE multi zero";
    qp_syndroms[0x17] = "[RX] pkt err, WQE Write send big";
    qp_syndroms[0x18] = "[RX] pkt err, WQE multi big";

    /* QPC errors */
    qp_syndroms[0x40] = "[qpc] [TMR] max-retry-cnt exceeded";
    qp_syndroms[0x41] = "[qpc] [req DB] QP not valid";
    qp_syndroms[0x42] = "[qpc] [req DB] security check";
    qp_syndroms[0x43] = "[qpc] [req DB] PI > last-index";
    qp_syndroms[0x44] = "[qpc] [req DB] wq-type is READ";
    qp_syndroms[0x45] = "[qpc] [req TX] QP not valid";
    qp_syndroms[0x46] = "[qpc] [req TX] Rendezvous WQE but wq-type is not WRITE";
    qp_syndroms[0x47] = "[qpc] [req RX] QP not valid";
    qp_syndroms[0x48] = "[qpc] [req RX] max-retry-cnt exceeded";
    qp_syndroms[0x49] = "[qpc] [req RDV] QP not valid";
    qp_syndroms[0x4a] = "[qpc] [req RDV] wrong wq-type";
    qp_syndroms[0x4b] = "[qpc] [req RDV] PI > last-index";
    qp_syndroms[0x4c] = "[qpc] [res TX] QP not valid";
    qp_syndroms[0x4d] = "[qpc] [res RX] max-retry-cnt exceeded";

    /* tx packet error */
    qp_syndroms[0x80] = "[TX] pkt error, QPC.wq_type is write does not support WQE.opcode";
    qp_syndroms[0x81] = "[TX] pkt error, QPC.wq_type is rendezvous does not support WQE.opcode";
    qp_syndroms[0x82] = "[TX] pkt error, QPC.wq_type is read does not support WQE.opcode";
    qp_syndroms[0x83] = "[TX] pkt error, QPC.gaudi1 is set does not support WQE.opcode";
    qp_syndroms[0x84] = "[TX] pkt error, WQE.opcode is write but WQE.size is 0";
    qp_syndroms[0x85] = "[TX] pkt error, WQE.opcode is multi-stride|local-stride|multi-dual but WQE.size is 0";
    qp_syndroms[0x86] = "[TX] pkt error, WQE.opcode is send but WQE.size is 0";
    qp_syndroms[0x87] = "[TX] pkt error, WQE.opcode is rendezvous-write|rendezvous-read but WQE.size is 0";
    qp_syndroms[0x88] = "[TX] pkt error, WQE.opcode is write but size > configured max-write-send-size";
    qp_syndroms[0x89] = "[TX] pkt error, WQE.opcode is multi-stride|local-stride|multi-dual but size > configured max-stride-size";
    qp_syndroms[0x8a] = "[TX] pkt error, WQE.opcode is rendezvous-write|rendezvous-read but QPC.remote_wq_log_size <= configured min-remote-log-size";
    qp_syndroms[0x8b] = "[TX] pkt error, WQE.opcode is rendezvous-write but WQE.size != configured rdv-wqe-size (per granularity)";
    qp_syndroms[0x8c] = "[TX] pkt error, WQE.opcode is rendezvous-read but WQE.size != configured rdv-wqe-size (per granularity)";
    qp_syndroms[0x8d] = "[TX] pkt error, WQE.inline is set but WQE.size != configured inline-wqe-size (per granularity)";
    qp_syndroms[0x8e] = "[TX] pkt error, QPC.gaudi1 is set but WQE.inline is set";
    qp_syndroms[0x8f] = "[TX] pkt error, WQE.opcode is multi-stride|local-stride|multi-dual but QPC.swq_granularity is 0";
    qp_syndroms[0x90] = "[TX] pkt error, WQE.opcode != NOP but WQE.reserved0 != 0";
    qp_syndroms[0x91] = "[TX] pkt error, WQE.opcode != NOP but WQE.wqe_index != execution-index [7.0]";
    qp_syndroms[0x92] = "[TX] pkt error, WQE.opcode is multi-stride|local-stride|multi-dual but WQE.size < stride-size";
    qp_syndroms[0x93] = "[TX] pkt error, WQE.reduction_opcode is upscale but WQE.remote_address LSB is not 0";
    qp_syndroms[0x94] = "[TX] pkt error, WQE.reduction_opcode is upscale but does not support WQE.opcode";
    qp_syndroms[0x95] = "[TX] pkt error, RAW packet but WQE.size not supported";
    qp_syndroms[0xA0] = "WQE.opcode is QoS but WQE.inline is set";
    qp_syndroms[0xA1] = "WQE.opcode above 15";
    qp_syndroms[0xA2] = "RAW above MIN";
    qp_syndroms[0xA3] = "RAW below MAX";
    qp_syndroms[0xA4] = "WQE.reduction is disable but reduction-opcode is not 0";
    qp_syndroms[0xA5] = "WQE.opcode is READ-RDV but WQE.inline is set";
    qp_syndroms[0xA7] = "WQE fetch WR size not 4";
    qp_syndroms[0xA8] = "WQE fetch WR addr not mod4";
    qp_syndroms[0xA9] = "RDV last-index";
    qp_syndroms[0xAA] = "Gaudi1 multi-dual";
    qp_syndroms[0xAB] = "WQE bad opcode";
    qp_syndroms[0xAC] = "WQE bad size";
    qp_syndroms[0xAD] = "WQE SE not RAW";
    qp_syndroms[0xAE] = "Gaudi1 tunnal";
    qp_syndroms[0xAF] = "Tunnel 0-size";
    qp_syndroms[0xB0] = "Tunnel max size";
};

#define SYNDROME_TYPE(syndrome) (((syndrome) >> 6) & 0x3)

const char* parse_qp_syndrome(uint32_t syndrome)
{
    int syndrome_type;
    const char* str = nullptr;

    /* syndrome comprised from 8 bits
     * [2:type, 6:syndrome]
     * 6 bits for syndrome
     * 2 bits for type
     *   0 - rx packet error
     *   1 - qp error
     *   2 - tx packet error
     */

    if (syndrome >= MAX_SYNDROMS)
    {
        syndrome_type = SYNDROME_TYPE(syndrome);

        switch (syndrome_type) {
        case 0:
            str = "RX packet syndrome unknown";
            break;
        case 1:
            str = "QPC syndrome unknown";
            break;
        case 2:
            str = "TX packet syndrome unknown";
            break;
        default:
            str = "syndrome unknown";
            break;
        }
    }
    else
    {
        str = qp_syndroms[syndrome];
    }

    return str;
}

void hcl_ibverbs_t::eq_poll(bool& stop, uint32_t _usleep)
{
    LOG_IBV("");

    init_error_tables();

    poll_stop_ = &stop;
    poll_done_ = false;

    ibv_async_event ibev = {};
    pollfd          pfd  = {};

    auto flgs = fcntl(ibctx_->async_fd, F_GETFL);
    int  rc   = fcntl(ibctx_->async_fd, F_SETFL, flgs | O_NONBLOCK);
    VERIFY(rc == 0, "fcntl failed: {}", rc);

    pfd.fd      = ibctx_->async_fd;
    pfd.events  = POLLIN;
    pfd.revents = 0;

    while (!stop)
    {
        rc = poll(&pfd, 1, _usleep / 1000);  // usec -> msec
        if (!rc) continue;

        if (ibctx_ == nullptr)
        {
            WRN_IBV("ibctx_ is nullptr (disappeared from under our feet). Did you properly destroy SynapseAI?");
            break;
        }
        /* Read an asynchronous event for this context */
        rc = ibv_.ibv_get_async_event(ibctx_, &ibev);
        if (rc) continue;

        if (parse_ib_eqe(&ibev))
        {
            hclNotifyFailureV2(DfaErrorCode::hclFailed, 0, "IB Async Event. DFA triggered");
            std::terminate();
        }
    }

    poll_done_ = true;
}

// returning true will trigger termination
bool hcl_ibverbs_t::parse_ib_eqe(ibv_async_event* event)
{
    union port_data_t
    {
        struct
        {
            uint32_t port : 16;  // 0  - 15
            uint32_t extd : 16;  // 16 - 31
        };
        uint32_t _raw = 0;
        port_data_t(int _val) : _raw(_val) {};
    };

    bool triggerDFA = true;

    /* There is no way to pass syndrome value to user via IBv
     * QP error syndromes will be printed to the dmesg.
     */

    switch (event->event_type)
    {
        case IBV_EVENT_QP_FATAL:
        case IBV_EVENT_QP_REQ_ERR:
        case IBV_EVENT_QP_LAST_WQE_REACHED:
        case IBV_EVENT_PATH_MIG:
        case IBV_EVENT_PATH_MIG_ERR:
        {
            hbldv_query_qp_attr dv_qp_attr   = {};
            ibv_qp_init_attr    qp_init_attr = {};
            ibv_qp_attr         qp_attr      = {};

            ibv_qp* evqp = event->element.qp;

            ibv_.hbldv_query_qp(evqp, &dv_qp_attr);
            ibv_.ibv_query_qp(evqp, &qp_attr, IBV_QP_PORT | IBV_QP_ACCESS_FLAGS, &qp_init_attr);

            uint32_t syndrome = qp_attr.qp_access_flags;
            uint32_t nic      = port2nic_[qp_attr.port_num];

            CRT_IBV("{}. QP({}), NIC({}), STATE({}), SYNDROME({}): {}",
                    err2str[event->event_type],
                    dv_qp_attr.qp_num,
                    nic,
                    evqp->state,
                    syndrome,
                    parse_qp_syndrome(syndrome));
            break;
        }

        case IBV_EVENT_CQ_ERR:
        {
            ibv_cq* evcq = event->element.cq;
            CRT_IBV("CQ error. CQ({}) CQE({})", evcq, evcq->cqe);
            break;
        }

        case IBV_EVENT_PORT_ACTIVE:
        case IBV_EVENT_PORT_ERR:
            triggerDFA = false;
            INF_IBV("{}, NIC({})", err2str[event->event_type], port2nic_[event->element.port_num]);
            break;

        case IBV_EVENT_SM_CHANGE:
        {
            port_data_t data(event->element.port_num);
            CRT_IBV("NIC({}) got completion on congestion CQ({})", port2nic_[data.port], data.extd);
            break;
        }

        case IBV_EVENT_DEVICE_FATAL:
            /* if port was provided by the driver, it is probably FIFO-error */
            if (event->element.port_num)
            {
                port_data_t data(event->element.port_num);
                CRT_IBV("NIC({}) got DB_FIFO({}) error", port2nic_[data.port], data.extd);
            }
            else
            {
                CRT_IBV("Device fatal event");
            }
            break;

        case IBV_EVENT_GID_CHANGE:
            triggerDFA = false;
            INF_IBV("{}, NIC({}), reparsing data.", err2str[event->event_type], port2nic_[event->element.port_num]);
            parse_sysfs_infiniband();
            break;

        default:
            CRT_IBV("Unknown event ({})", event->event_type);
            break;
    }

    ibv_.ibv_ack_async_event(event);

    return triggerDFA;
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
            auto index = atoi(entry->d_name);

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
