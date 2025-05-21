#pragma once

#include "hcl_ibv_loader.h"
#include "hcl_types.h"  // for eIbvNicPhysicalState
#include "hccl_types.h"
#include "infiniband/hbldv.h"
#include "scal.h"
#include <unordered_map>
#include <map>
#include <vector>
#include "interfaces/hcl_idevice.h"

enum eNicType
{
    ntGeneric,
    ntCollective,
    ntScaleOut
};

using comm_t   = HCL_Comm;
using device_t = IHclDevice*;

class hcl_ibverbs_t
{
public:
    virtual ~hcl_ibverbs_t() noexcept(false) { close(); }

    hcclResult_t init(const HclDeviceConfig& deviceConfig);
    void         close();

    void on_comm_init(comm_t comm);
    void on_comm_destroy(comm_t comm);

    bool is_nic_up(uint32_t nic);
    void setup_nic(uint32_t nic, uint32_t num_wqes, uint32_t bp, eNicType nt);
    void create_cq(uint32_t nic, int num_cqes = 4);

    const eIbvNicPhysicalState get_nic_phys_state(const uint32_t nic);

    uint32_t create_qp(comm_t comm, bool sender, uint32_t nic, uint32_t qpHint = 0);
    uint32_t reserve_collective_qp(bool is_scale_out);

    uint32_t create_migration_qp(comm_t   comm,
                                 bool     sender,
                                 uint32_t nic,
                                 uint32_t migratedNic,
                                 uint32_t migratedQpn,
                                 uint32_t qpHint = 0);

    void set_migration_qp_rtr(comm_t   comm,
                              uint32_t nic,
                              uint32_t qpn,
                              uint32_t migratedNic,
                              uint32_t migratedQpn,
                              uint32_t src_ip,
                              uint64_t src_mac,
                              uint32_t dst_ip,
                              uint64_t dst_mac,
                              uint32_t dst_qp);

    void set_migration_qp_rts(comm_t comm, uint32_t nic, uint32_t qpn, uint32_t migratedNic, uint32_t migratedQpn);
    void migrate_qp(comm_t comm, uint32_t nic, uint32_t qpn);
    void destroy_qp(comm_t comm, uint32_t nic, uint32_t qpn);

    void set_qp_ctx(comm_t   comm,
                    uint32_t qpn,
                    uint32_t nic,
                    uint32_t src_ip,
                    uint64_t src_mac,
                    uint32_t dst_ip,
                    uint64_t dst_mac,
                    uint32_t dst_qp,
                    uint8_t  lagIdx,
                    uint8_t  lastInLag);

    void     eq_poll(bool& stop, uint32_t _usleep);
    uint32_t get_qp_offset(uint32_t nic);

    void create_fifos(scal_handle_t scal_handle);
    void get_port_mask(portMaskConfig& portsMasks);

    /**
     * @brief This function converts a NIC mask to an InfiniBand (IB) port mask and updates
     *        the corresponding scaleup or scaleout port mask based on the flag.
     *
     * @param nicMask The NIC mask to be converted.
     * @param isScaleout If true, the function updates the scaleout port mask;
     *                   otherwise, it updates the scaleup port mask.
     */
    void nic_mask_to_ib_port_mask(const nics_mask_t& nicMask, bool isScaleout);
    void set_hcl_device(IHclDevice* device);

    ibv_context* get_ibv_context() const { return ibctx_; }
    void*        get_lib_handle() const { return ibv_.lib_handle(); }
    bool         has_ib_device() const { return ibctx_ != nullptr; }

private:
    union ibvqp_key_t
    {
        struct
        {
            uint32_t nic;
            uint32_t qpn;
        };
        uint64_t _raw = -1;
        ibvqp_key_t(uint32_t _n, uint32_t _q) : nic(_n), qpn(_q) {};
        ibvqp_key_t(uint64_t _nq) : _raw(_nq) {};
        operator uint64_t() { return _raw; }
    };

    // key is nic + qpn
    using qp_map_t = std::unordered_map<uint64_t, ibv_qp*>;
    class ibvqp_map_t : public qp_map_t
    {
    public:
        ibv_qp* operator()(uint32_t nic, uint32_t qpn) const { return qp_map_t::at(ibvqp_key_t(nic, qpn)); };
        ibv_qp* at(uint32_t nic, uint32_t qpn) const { return qp_map_t::at(ibvqp_key_t(nic, qpn)); };
        void    erase(uint32_t nic, uint32_t qpn) { qp_map_t::erase(ibvqp_key_t(nic, qpn)); };
        void    emplace(uint32_t nic, uint32_t qpn, ibv_qp* ibqp) { qp_map_t::emplace(ibvqp_key_t(nic, qpn), ibqp); }
    };

    bool         init_   = false;
    device_t     device_ = nullptr;
    ibv_context* ibctx_  = nullptr;
    ibv_pd*      ibpd_   = nullptr;

    ibv_lib_t& ibv_ = g_ldr;

    class comm_ibvqps_t
    {
    private:
        std::unordered_map<comm_t, ibvqp_map_t> map_;
        mutable lock_t                          lock_;

    public:
        auto& at(comm_t comm)
        {
            locker_t locker(lock_);
            return map_.at(comm);
        }

        void erase(comm_t comm)
        {
            locker_t locker(lock_);
            map_.erase(comm);
        }

        auto exists(comm_t comm) const
        {
            locker_t locker(lock_);
            return map_.find(comm) != map_.end();
        }

        auto emplace(comm_t comm, ibvqp_map_t&& ibvqps)
        {
            locker_t locker(lock_);
            return map_.emplace(comm, std::move(ibvqps));
        }

        void clear()
        {
            locker_t locker(lock_);
            map_.clear();
        }

        auto& operator()() { return map_; }
    };

    using cq_array_t   = std::vector<ibv_cq*>;
    using fifo_array_t = std::vector<hbldv_usr_fifo*>;

    comm_ibvqps_t qps_;
    cq_array_t    cqs_;
    fifo_array_t  fifos_;

    bool* poll_stop_ = nullptr;
    bool  poll_done_ = true;

    bool dram_enabled_ = true;

    bool parse_ib_eqe(ibv_async_event* event);
    void report_nic_status(const ibv_event_type& event, const uint32_t nic);

    int     sgid_index(uint32_t dst_ip, uint32_t src_ip, uint64_t src_mac, uint32_t nic);
    ibv_gid dgid(uint32_t dst_ip, uint64_t dst_mac);

    struct sysfs_gid_t
    {
        ibv_gid            gid  = {};
        ibv_gid_type_sysfs type = IBV_GID_TYPE_SYSFS_UNDEFINED;
    };
    using sysfs_ports_t = std::map<uint32_t, std::map<uint32_t, sysfs_gid_t>>;

    std::string   ib_devname_;
    sysfs_ports_t sysfs_ports_;
    void          parse_sysfs_infiniband();
    void          walk_fs(const std::string& path, uint32_t port = 0);
    void          map_ib_ports(const nics_mask_t nics_mask);
    uint64_t      getHwIpPortMask(const int fd) const;

    nics_mask_t scaleup_ports_;   // IB scaleup ports mask after LKD, HCL Mask
    nics_mask_t scaleout_ports_;  // IB scaleout ports mask after LKD, HCL Mask

    std::vector<int32_t> nic2port_;
    std::vector<int32_t> port2nic_;
};

extern hcl_ibverbs_t g_ibv;
