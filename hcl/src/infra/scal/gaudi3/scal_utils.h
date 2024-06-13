#pragma once

#include <cstdint>
#include "infra/scal/gen2_arch_common/scal_utils.h"

namespace hcl
{
class Gaudi3HclScalUtils : public Gen2ArchScalUtils
{
public:
    virtual uint64_t    calculateSoAddressFromIdxAndSM(unsigned smIdx, unsigned idx) override;
    virtual unsigned    getSOBIndex(uint32_t addr) override;
    virtual sob_info    getSOBInfo(uint32_t addr) override;
    virtual std::string printSOBInfo(uint32_t addr) override;
    virtual std::string printSOBInfo(sob_info sob) override;
};

};  // namespace hcl