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
    virtual sob_info    getSOBInfo(uint32_t addr)                          = 0;
    virtual std::string printSOBInfo(uint32_t addr)                        = 0;
    virtual std::string printSOBInfo(sob_info sob)                         = 0;

    // platform dependent COMP_SYNC_GROUP_CMAX_TARGET value from QMAN FW
    virtual uint32_t getCMaxTargetValue() = 0;
};