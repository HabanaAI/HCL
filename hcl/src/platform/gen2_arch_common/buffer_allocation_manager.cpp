#include "buffer_allocation_manager.h"
#include "platform/gen2_arch_common/collective_states.h"

BufferAllocationManager::BufferAllocationManager()
{
    m_repetitions = 0;
    m_nextBufferToAllocateIndex = 0;
}

unsigned BufferAllocationManager::alloc(DeviceBufferManager&                deviceBufferManager,
                                        hcl::syncInfo&                      longSo,
                                        int64_t                             cgSize,
                                        unsigned                            requiredExtraCredits,
                                        std::vector<std::pair<bool, bool>>& ltuValid)
{
    VERIFY(m_repetitions > 0, "trying to alloc buffers without registration");

    unsigned bufferAllocationIndex;
    uint64_t  lastTargetVal;
    int64_t  signalsDiff;

    for (bufferAllocationIndex = 0; bufferAllocationIndex < m_nextBufferToAllocateIndex; bufferAllocationIndex++)
    {
        lastTargetVal = deviceBufferManager.allocNextBuffer(longSo.targetValue +
                                                                m_allocations[bufferAllocationIndex].m_iterations,
                                                            m_allocations[bufferAllocationIndex].m_poolId);

        if (m_allocations[bufferAllocationIndex].m_poolId == SCALEUP_RR_AND_ALL2ALL_POOL)
        {
            unsigned currentBufferIdx =
                deviceBufferManager.getCurrentBufferIdx(m_allocations[bufferAllocationIndex].m_poolId);
            if (m_allocations[bufferAllocationIndex].dontWaitOnCg)
            {
                ltuValid[currentBufferIdx].first  = ltuValid[currentBufferIdx].second;
                ltuValid[currentBufferIdx].second = true;

                lastTargetVal = ltuValid[currentBufferIdx].first ? 0 : lastTargetVal;
            }
            else
            {
                ltuValid[currentBufferIdx].first  = false;
                ltuValid[currentBufferIdx].second = false;
            }
        }
        else
        {
            VERIFY(!m_allocations[bufferAllocationIndex].dontWaitOnCg, "LTU only supports up buffer pool!");
        }

        if (lastTargetVal != 0)
        {
            VERIFY(longSo.targetValue > lastTargetVal,
                   "No Available intermediate buffer");  // Check if lastTargetVal < 0
            signalsDiff = longSo.targetValue - lastTargetVal;
            if ((cgSize - signalsDiff) > 0 && (cgSize - signalsDiff) > requiredExtraCredits)
            {
                requiredExtraCredits = (unsigned)(cgSize - signalsDiff);
            }
        }
        LOG_TRACE(HCL_ECR, "IMB allocation: pool {}, iterations {}, current so {}, required extra credits {}",
                  m_allocations[bufferAllocationIndex].m_poolId,
                  m_allocations[bufferAllocationIndex].m_iterations,
                  longSo.targetValue,
                  requiredExtraCredits);
    }
    m_repetitions--;

    return requiredExtraCredits;
}

void BufferAllocationManager::addAllocation(e_devicePoolID poolId, unsigned int numIterations, bool dontWaitOnCg)
{
    VERIFY(m_nextBufferToAllocateIndex < MAX_BUFFERS_TO_ALLOCATE, "trying to allocate more than {} buffers. pool {}",
           MAX_BUFFERS_TO_ALLOCATE, poolId);
    m_allocations[m_nextBufferToAllocateIndex] = {.m_poolId     = poolId,
                                                  .m_iterations = numIterations,
                                                  .dontWaitOnCg = dontWaitOnCg};
    LOG_TRACE(HCL_ECR, "registering pool {} for {} iterations, dontWaitOnCg={}", poolId, numIterations, dontWaitOnCg);
    m_nextBufferToAllocateIndex++;
}

void BufferAllocationManager::setRepetitions(unsigned int repetitions)
{
    m_repetitions = repetitions;
}

bool BufferAllocationManager::isValid() const
{
    return m_repetitions > 0;
}

void BufferAllocationManager::reset()
{
    m_nextBufferToAllocateIndex = 0;
}

void BufferAllocationManager::registerStaticBuffersAllocations(CommonState& commonState, unsigned boxIter)
{
    // if valid - #repetitions > 0 - perform previous iteration again instead of creating a new one
    if (isValid())
    {
        return;
    }
    // not valid - create new iteration
    reset();
    switch (commonState.m_currentOp)
    {
        case eHCLReduceScatter:
        {
            unsigned numRepetitions = 1;
            unsigned numBoxes       = commonState.m_boxIterations;

            if (boxIter == 0)
            {
                if (commonState.m_isMultiScaleupGroup)
                {
                    unsigned numIterations = numBoxes - 1;

                    if (commonState.m_collectiveOp == eHCLReduce)
                    {
                        // There is a single gather iteration it does scaleout
                        numIterations += 1;
                    }
                    else if (commonState.m_collectiveOp == eHCLAllReduce)
                    {
                        numIterations++;
                    }
                    addAllocation(SCALEOUT_RR_POOL, numIterations);
                }
            }
            else  // boxIter > 0
            {
                numRepetitions = numBoxes - 1;
            }

            if (commonState.m_dynamicComm.getScaleupGroupSize() > 1)
            {
                if (!commonState.m_isMultiScaleupGroup && commonState.isComplexImplementation() && !commonState.isRoot())
                {
                    addAllocation(SCALEUP_RR_AND_ALL2ALL_POOL, 1);
                }
                else
                {
                    addAllocation(SCALEUP_RR_AND_ALL2ALL_POOL, 0, commonState.m_syncUpBufferWithLtu);
                }
            }
            if (commonState.m_collectiveOp == eHCLReduce && !commonState.isRoot() && commonState.m_16BitReduction)
            {
                if (commonState.m_isMultiScaleupGroup && boxIter == (numBoxes - 1))
                {
                    addAllocation(REDUCE_RR_POOL, 1);
                    numRepetitions = 1;
                }
                else if (boxIter > 0)
                {
                    numRepetitions--;
                }
            }
            setRepetitions(numRepetitions);
            break;
        }
        case eHCLGather:
        {
            if (boxIter > 0 && commonState.m_dynamicComm.getMyScaleupGroup() == commonState.rootBox() && !commonState.isRoot())
            {
                addAllocation(REDUCE_RR_POOL, 0);
                setRepetitions(1);
            }
            break;
        }
        case eHCLAll2All:
        {
            if (boxIter > 0 && commonState.m_dynamicComm.getScaleupGroupSize() > 1 && commonState.m_all2allIter == 0)
            {
                addAllocation(SCALEUP_RR_AND_ALL2ALL_POOL, commonState.m_all2allIterations - 1);
                setRepetitions(1);
            }
            break;
        }
        case eHCLReduce:
        case eHCLAllReduce:
        case eHCLAllGather:
        case eHCLBroadcast:
        case eHCLSinglePeerBroadcast:
        case eHCLSimpleBroadcast:
        case eHCLScatter:
        case eHCLNoCollective:
            break;
        case eHCLCollectiveLastValue:
            VERIFY(false, "wrong collective value at registerStaticBuffersAllocations");
    }
}
