#pragma once
#include <cstdint>  // for uint32_t, uint64_t
#include "hcl_utils.h"
#include "platform/gen2_arch_common/commands/hcl_commands_types.h"

class HclCommandsGen2Arch;

class HclLbwWriteAggregator
{
public:
    HclLbwWriteAggregator(hcl::ScalStream* scalStream, unsigned schedIdx, HclCommandsGen2Arch& commands);
    HclLbwWriteAggregator(HclLbwWriteAggregator&&)                 = delete;
    HclLbwWriteAggregator(const HclLbwWriteAggregator&)            = delete;
    HclLbwWriteAggregator& operator=(HclLbwWriteAggregator&&)      = delete;
    HclLbwWriteAggregator& operator=(const HclLbwWriteAggregator&) = delete;
    void                   aggregate(uint32_t destination, uint32_t data);
    virtual ~HclLbwWriteAggregator();

private:
    LBWBurstDestData_t   m_burstContainer;
    hcl::ScalStream*     m_scalStream;
    unsigned             m_schedIdx;
    HclCommandsGen2Arch& m_commands;
};
