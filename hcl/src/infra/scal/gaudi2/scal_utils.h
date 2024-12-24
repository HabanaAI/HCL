#pragma once

#include <cstdint>
#include "infra/scal/gen2_arch_common/scal_utils.h"

namespace hcl
{
class Gaudi2HclScalUtils : public Gen2ArchScalUtils
{
public:
    virtual uint64_t    calculateSoAddressFromIdxAndSM(unsigned smIdx, unsigned idx) override;
    virtual unsigned    getSOBIndex(uint32_t addr) override;
    virtual SobInfo     getSOBInfo(uint32_t addr) override;
    virtual std::string printSOBInfo(uint32_t addr) override;
    virtual std::string printSOBInfo(SobInfo sob) override;
    virtual std::string printMonArmInfo(unsigned smIdx, uint32_t monIdx) override;
    virtual uint32_t    getCMaxTargetValue() override;
};

};  // namespace hcl