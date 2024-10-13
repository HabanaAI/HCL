#pragma once

#include "platform/gen2_arch_common/gen2arch_nic.h"

class Gaudi2Nic : public Gen2ArchNic
{
public:
    Gaudi2Nic(IHclDevice* device, uint32_t nic, uint32_t nQPN, uint32_t bp);
};
