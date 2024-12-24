#pragma once
#include "platform/gen2_arch_common/hcl_address_generator.h"  // for HclAddressGenerator

class CommonState;

class HclAddressGeneratorGaudi2 : public HclAddressGenerator
{
public:
    HclAddressGeneratorGaudi2() : HclAddressGenerator() {};
    virtual ~HclAddressGeneratorGaudi2() = default;

    virtual uint64_t recalcAddressForDisregardRank(const HCL_CollectiveOp currentOp,
                                                   const uint64_t         address,
                                                   const uint64_t         offset) override
    {
        return address;
    }

private:
};