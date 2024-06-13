#pragma once

#include "platform/gen2_arch_common/gen2arch_nic.h"

class Gaudi2Nic : public Gen2ArchNic
{
public:
    Gaudi2Nic(IHclDevice* device, int nic, uint32_t nQPN, uint32_t bp)
    : Gen2ArchNic(device, nic, nQPN, HL_NIC_USER_WQ_SEND, HL_NIC_USER_WQ_RECV, bp)
    {
    }
};

class Gaudi2IBVNic : public Gen2ArchIBVNic
{
public:
    Gaudi2IBVNic(IHclDevice* device, uint32_t nic, uint32_t nQPN, uint32_t bp)
    : Gen2ArchIBVNic(device, nic, nQPN, bp, ntGeneric)
    {
    }
};
