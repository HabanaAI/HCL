#pragma once
#include "platform/gen2_arch_common/hcl_address_generator.h"  // for HclAddressGenerator

class CommonState;

class HclAddressGeneratorGaudi3 : public HclAddressGenerator
{
public:
    HclAddressGeneratorGaudi3() : HclAddressGenerator() {};
    virtual ~HclAddressGeneratorGaudi3() = default;

    virtual uint64_t recalcAddressForDisregardRank(const HCL_CollectiveOp currentOp,
                                                   const uint64_t         address,
                                                   const uint64_t         offset) override;

private:
};