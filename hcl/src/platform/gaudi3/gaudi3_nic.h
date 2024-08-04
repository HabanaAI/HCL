#pragma once

#include "platform/gen2_arch_common/gen2arch_nic.h"

class Gaudi3Nic : public Gen2ArchNic
{
public:
    uint32_t nic2QpOffset = -1;

    Gaudi3Nic(IHclDevice* device, uint32_t nic, uint32_t nQPN, bool scaleOut, uint32_t bp);

    virtual void init() override;
};