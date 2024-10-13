
#include "gaudi3_nic.h"
#include "interfaces/hcl_idevice.h"
#include "ibverbs/hcl_ibverbs.h"

Gaudi3Nic::Gaudi3Nic(IHclDevice* device, uint32_t nic, uint32_t nQPN, bool scaleOut, uint32_t bp)
: Gen2ArchNic(device, nic)
{
    g_ibv.setup_nic(nic, nQPN, bp, scaleOut ? ntScaleOut : ntCollective);
};

void Gaudi3Nic::init()
{
    Gen2ArchNic::init();
    nic2QpOffset = g_ibv.get_qp_offset(m_nic);
};