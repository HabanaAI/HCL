#pragma once

#include "hcl_ibv_loader.h"
#include "hcl_types.h"
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

class hcl_ibverbs_t
{
public:
    virtual ~hcl_ibverbs_t() noexcept(false) { close(); }

    hcclResult_t init(IHclDevice* device);
    void close();

    bool is_nic_up(uint32_t nic);
    void setup_nic(uint32_t nic, uint32_t num_wqes, uint32_t bp, eNicType nt);
    void create_cq(uint32_t nic, int num_cqes = 4);

    uint32_t create_qp(bool sender, uint32_t nic, uint32_t qpHint = 0);
    uint32_t create_collective_qp(bool is_scale_out);

    void destroy_qp(uint32_t nic, uint32_t qpn);
    void set_qp_ctx(uint32_t qpn,
                    uint32_t nic,
                    uint32_t src_ip,
                    uint64_t src_mac,
                    uint32_t dst_ip,
                    uint64_t dst_mac,
                    uint32_t dst_qp,
                    uint8_t lagIdx,
                    uint8_t lastInLag);

    void     eq_poll(bool& stop, uint32_t _usleep);
    uint32_t get_qp_offset(uint32_t nic);

    void     create_fifos(scal_handle_t scal_handle);

    operator ibv_context*() { return ibctx_; }

private:
    class ibvqp_map_t : public std::unordered_map<uint64_t, ibv_qp*>
    {
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
            operator uint64_t() { return _raw; }
        };

    public:
        ibv_qp* operator()(uint32_t nic, uint32_t qpn) { return at(ibvqp_key_t(nic, qpn)); };
        void erase(uint32_t nic, uint32_t qpn) { std::unordered_map<uint64_t, ibv_qp*>::erase(ibvqp_key_t(nic, qpn)); };
        void emplace(uint32_t nic, uint32_t qpn, ibv_qp* ibqp) { std::unordered_map<uint64_t, ibv_qp*>::emplace(std::make_pair(ibvqp_key_t(nic, qpn), ibqp)); };
    };

    using fifo_array_t = std::vector<hbldv_usr_fifo*>;
    using cq_array_t   = std::vector<ibv_cq*>;

    IHclDevice*  device_ = nullptr;
    ibv_context* ibctx_  = nullptr;
    ibv_pd*      ibpd_   = nullptr;

    ibv_lib_t&   ibv_ = g_ldr;
    ibvqp_map_t  qps_;
    cq_array_t   cqs_;
    fifo_array_t fifos_;

    bool* poll_stop_ = nullptr;
    bool  poll_done_ = true;

    bool dram_enabled_ = true;

    bool parse_ib_eqe(ibv_async_event* event);

    int sgid_index(uint32_t dst_ip, uint32_t src_ip, uint64_t src_mac, uint32_t nic);
    ibv_gid dgid(uint32_t dst_ip, uint64_t dst_mac);

    struct sysfs_gid_t
    {
        ibv_gid             gid = {};
        ibv_gid_type_sysfs  type = IBV_GID_TYPE_SYSFS_UNDEFINED;
    };
    using sysfs_ports_t = std::map<uint32_t, std::map<uint32_t, sysfs_gid_t>>;

    std::string ib_devname_;
    sysfs_ports_t sysfs_ports_;
    void parse_sysfs_infiniband();
    void walk_fs(const std::string& path, uint32_t port = 0);
    void map_ib_ports(const nics_mask_t nics_mask);

    std::vector<int32_t> nic2port_;
    std::vector<int32_t> port2nic_;
};

extern hcl_ibverbs_t g_ibv;
