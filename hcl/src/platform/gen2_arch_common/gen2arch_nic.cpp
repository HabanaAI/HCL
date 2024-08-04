
#include "gen2arch_nic.h"
#include "interfaces/hcl_idevice.h"

Gen2ArchNic::Gen2ArchNic(IHclDevice* device, uint32_t nic, uint32_t nQPN, uint32_t bp, eNicType nt)
: IHclNic(device, nic)
{
    g_ibv.setup_nic(m_nic, nQPN, bp, nt);
};

void Gen2ArchNic::init()
{
    g_ibv.create_cq(m_nic);
};
