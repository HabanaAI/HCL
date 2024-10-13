
#include "gen2arch_nic.h"
#include "interfaces/hcl_idevice.h"

Gen2ArchNic::Gen2ArchNic(IHclDevice* device, uint32_t nic) : IHclNic(device, nic) {};

void Gen2ArchNic::init()
{
    g_ibv.create_cq(m_nic);
};
