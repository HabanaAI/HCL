#pragma once

#include <iostream>
#include <cstdint>

#include "platform/gen2_arch_common/types.h"

#define varoffsetof(t, m) ((size_t)(&(((t*)0)->m)))

class Gen2ArchScalUtils
{
public:
    virtual ~Gen2ArchScalUtils() = default;

    virtual uint64_t    calculateSoAddressFromIdxAndSM(unsigned, unsigned) = 0;
    virtual unsigned    getSOBIndex(uint32_t addr)                         = 0;
    virtual SobInfo     getSOBInfo(uint32_t addr)                          = 0;
    virtual std::string printSOBInfo(uint32_t addr)                        = 0;
    virtual std::string printSOBInfo(SobInfo sob)                          = 0;
    virtual std::string printMonArmInfo(unsigned smIdx, uint32_t monIdx)   = 0;

    // platform dependent COMP_SYNC_GROUP_CMAX_TARGET value from QMAN FW
    virtual uint32_t getCMaxTargetValue() = 0;
};