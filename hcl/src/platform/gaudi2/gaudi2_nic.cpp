#include "gaudi2_nic.h"
#include "ibverbs/hcl_ibverbs.h"

Gaudi2Nic::Gaudi2Nic(IHclDevice* device, uint32_t nic, uint32_t nQPN, uint32_t bp) : Gen2ArchNic(device, nic)
{
    g_ibv.setup_nic(nic, nQPN, bp, ntGeneric);
};