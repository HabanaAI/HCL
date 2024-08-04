#include "completion_group.h"
#include "infra/scal/gen2_arch_common/scal_wrapper.h"  // for Gen2ArchScalWr...

using namespace hcl;

CompletionGroup::CompletionGroup(Gen2ArchScalWrapper& scalWrapper, scal_comp_group_handle_t cg)
: m_scalWrapper(scalWrapper), m_lastFinidshedTargetValue(0), m_cg(cg)
{
}

void CompletionGroup::waitOnValue(uint64_t targetValue)
{
    if (targetValue <= m_lastFinidshedTargetValue)
    {
        return;
    }

    m_scalWrapper.waitOnCg(m_cg, targetValue);
    m_lastFinidshedTargetValue = targetValue;
}

void CompletionGroup::cgRegisterTimeStemp(uint64_t targetValue, uint64_t timestampHandle, uint32_t timestampsOffset)
{
    m_scalWrapper.completionGroupRegisterTimestamp(m_cg, targetValue, timestampHandle, timestampsOffset);
}

bool CompletionGroup::checkForTargetValue(uint64_t targetValue)
{
    if (targetValue <= m_lastFinidshedTargetValue)
    {
        return true;
    }

    if (m_scalWrapper.checkTargetValueOnCg(m_cg, targetValue))
    {
        m_lastFinidshedTargetValue = targetValue;
        return true;
    }
    return false;
}