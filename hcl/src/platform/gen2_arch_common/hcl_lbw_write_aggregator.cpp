#include "hcl_lbw_write_aggregator.h"
#include "infra/scal/gen2_arch_common/scal_stream.h"          // for ScalStream
#include "platform/gen2_arch_common/commands/hcl_commands.h"  // for HclComm.

HclLbwWriteAggregator::HclLbwWriteAggregator(hcl::ScalStream*     scalStream,
                                             unsigned             schedIdx,
                                             HclCommandsGen2Arch& commands,
                                             bool                 submitOnDestroy)
: m_scalStream(scalStream), m_schedIdx(schedIdx), m_commands(commands), m_submitOnDestroy(submitOnDestroy)
{
}

void HclLbwWriteAggregator::aggregate(uint32_t destination, uint32_t data)
{
    m_burstContainer.push_back({destination, data});
}

HclLbwWriteAggregator::~HclLbwWriteAggregator()
{
    if (m_submitOnDestroy && m_burstContainer.size() > 0)
    {
        m_commands.serializeLbwBurstWriteCommand(*m_scalStream, m_schedIdx, m_burstContainer);
    }
}

LBWBurstData_t* HclLbwWriteAggregator::getLbwBurstData()
{
    return &m_burstContainer;
}
