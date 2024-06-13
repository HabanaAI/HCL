
#include "gen2arch_nic.h"
#include "interfaces/hcl_idevice.h"

Gen2ArchNic::Gen2ArchNic(IHclDevice* device, int nic, uint32_t nQPN, uint32_t sendType, uint32_t recvType, uint32_t bp)
: HclNic(device, nic, nQPN, HL_NIC_MEM_DEVICE, sendType, recvType), m_bp(bp)
{
    _set_app_params_();
}

void Gen2ArchNic::_set_app_params_()
{
    struct hlthunk_nic_user_set_app_params_in params = {};

    params.advanced   = 1;
    params.bp_offs[0] = m_bp;

    int rc = hlthunk_nic_user_set_app_params(m_device->getFd(), m_nic, &params);
    VERIFY(rc == 0, "hlthunk_nic_user_set_app_params() failed: {}", rc);
}

Gen2ArchIBVNic::Gen2ArchIBVNic(IHclDevice* device, uint32_t nic, uint32_t nQPN, uint32_t bp, eNicType nt)
: IHclNic(device, nic)
{
    g_ibv.setup_nic(m_nic, nQPN, bp, nt);
};

void Gen2ArchIBVNic::init()
{
    g_ibv.create_cq(m_nic);
};
