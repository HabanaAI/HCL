#pragma once
#include "platform/gen2_arch_common/hcl_address_generator.h"  // for HclAddressGenerator

class CommonState;

class HclAddressGeneratorGaudi3 : public HclAddressGenerator
{
public:
    HclAddressGeneratorGaudi3() = delete;
    HclAddressGeneratorGaudi3(HclCommandsGen2Arch& commands) : HclAddressGenerator(commands) {};
    virtual ~HclAddressGeneratorGaudi3() = default;

    virtual uint64_t
    recalcAddressForDisragardRank(const HCL_CollectiveOp currentOp, const uint64_t address, const uint64_t offset);

private:
};