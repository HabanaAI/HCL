#pragma once
#include <cstdint>  // for uint32_t, uint64_t
#include "hcl_utils.h"
#include "platform/gen2_arch_common/commands/hcl_commands_types.h"

class HclCommandsGen2Arch;

class HclLbwWriteAggregator
{
public:
    HclLbwWriteAggregator(hcl::ScalStream*     scalStream,
                          unsigned             schedIdx,
                          HclCommandsGen2Arch& commands,
                          bool                 submitOnDestroy = true);
    HclLbwWriteAggregator(HclLbwWriteAggregator&&)                 = delete;
    HclLbwWriteAggregator(const HclLbwWriteAggregator&)            = delete;
    HclLbwWriteAggregator& operator=(HclLbwWriteAggregator&&)      = delete;
    HclLbwWriteAggregator& operator=(const HclLbwWriteAggregator&) = delete;
    void                   aggregate(uint32_t destination, uint32_t data);
    LBWBurstData_t*        getLbwBurstData();
    virtual ~HclLbwWriteAggregator();

private:
    LBWBurstData_t       m_burstContainer;
    hcl::ScalStream*     m_scalStream;
    unsigned             m_schedIdx;
    HclCommandsGen2Arch& m_commands;

    // We may want to submit the aggregated data via a different command,
    // this flag prevents the submission of the burst container once the aggregator is destroyed.
    bool m_submitOnDestroy = true;
};
