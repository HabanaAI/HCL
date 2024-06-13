#pragma once

#include <cstdint>  // for uint64_t, uint32_t
#include "scal.h"   // for scal_comp_group_handle_t
namespace hcl
{
class Gen2ArchScalWrapper;
}

namespace hcl
{
/**
 * @brief CompletionGroup class will manage target value of the given cg.
 *
 */
class CompletionGroup
{
public:
    CompletionGroup(Gen2ArchScalWrapper& scalWrapper, scal_comp_group_handle_t cg);
    CompletionGroup(CompletionGroup&&)      = delete;
    CompletionGroup(const CompletionGroup&) = delete;
    CompletionGroup& operator=(CompletionGroup&&) = delete;
    CompletionGroup& operator=(const CompletionGroup&) = delete;
    ~CompletionGroup()                                 = default;

    /**
     * @brief This is a blocking method and its doing 3 things:
     *        1. Update last known done target value.
     *        2. If targetValue <= m_lastFinidshedTargetValue will return immediately.
     *        3. If targetValue > m_lastFinidshedTargetValue, will block on host until device finishes job execution.
     *
     * @param targetValue [in] target value to wait/check if done
     */
    void waitOnValue(uint64_t targetValue);

    bool checkForTargetValue(uint64_t targetValue);

    void cgRegisterTimeStemp(uint64_t targetValue, uint64_t timestampHandle, uint32_t timestampsOffset);

private:
    Gen2ArchScalWrapper&     m_scalWrapper;
    uint64_t                 m_lastFinidshedTargetValue = 0;
    scal_comp_group_handle_t m_cg;
};
}  // namespace hcl
