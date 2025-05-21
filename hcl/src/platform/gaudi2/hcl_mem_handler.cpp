#include "platform/gaudi2/hcl_mem_handler.h"
#include "platform/gen2_arch_common/hcl_mem_handler.h"
#include "platform/gen2_arch_common/hcl_device_controller.h"
#include "platform/gen2_arch_common/simb_pool_container_allocator.h"

HclCollectiveMemHandlerGaudi2::HclCollectiveMemHandlerGaudi2(int                        archStreamId,
                                                             HclAddressGenerator&       addressGenerator,
                                                             DeviceSimbPoolManagerBase& deviceSimbPoolManager,
                                                             HclCommandsGen2Arch&       commands,
                                                             HclGraphSyncGen2Arch&      graphSync)
: HclCollectiveMemHandlerGen2Arch(archStreamId, addressGenerator, deviceSimbPoolManager, commands, graphSync)
{
}

void HclCollectiveMemHandlerGaudi2::generateBaseAddressOrSubBuffIdx(SliceState&       sliceState,
                                                                    unsigned int&     sliceIter,
                                                                    BoxNumInfo&       boxNumInfo,
                                                                    HCL_CollectiveOp& currentOp,
                                                                    uint64_t&         offset,
                                                                    uint64_t&         baseAddress,
                                                                    uint32_t&         subBuffIndex)
{
    if (!sliceState.m_isReductionCollective || currentOp == eHCLAllGather || currentOp == eHCLGather)
    {
        baseAddress =
            m_addressGenerator.generateScaleUpRecvAddress(sliceState, sliceIter, boxNumInfo, currentOp, offset);
        LOG_HCL_TRACE(HCL, "Setting scale-up receive base address to 0x{:x}", baseAddress);
    }
    else
    {
        subBuffIndex = m_addressGenerator.generateScaleUpRecvIndices(sliceState, m_archStreamId) /
                       DeviceSimbPoolManagerBase::getFactor(SCALEUP_AND_ALL2ALL_POOL);
        m_addressGenerator.addressContainer().consumeScaleUpRecvAddress(sliceState);
        LOG_HCL_TRACE(HCL, "Setting scale-up receive index to {}", subBuffIndex);
    }
}

void HclCollectiveMemHandlerGaudi2::memsetIMBs(SimbPoolContainerAllocator* simbPoolContainerAllocator,
                                               SignalsManager*             signalsManager,
                                               SliceState&                 sendSliceState,
                                               SliceState&                 recvSliceState,
                                               unsigned int                sizeInBytes,
                                               hcl::syncInfo               longSo,
                                               unsigned                    schedIdx,
                                               hcl::ScalStream&            garbageCollectionStream,
                                               HCL_StreamId                m_streamId,
                                               e_devicePoolID              poolId,
                                               uint8_t                     streamCtxtID,
                                               hcclDataType_t              dataType)
{
    if (m_deviceSimbPoolManager.isPoolAllocated(poolId) && m_deviceSimbPoolManager.bufferExpired(poolId))
    {
        // get relevant slice
        unsigned indexOfSubBuffer = m_deviceSimbPoolManager.getSliceId(poolId, m_streamId);

        // get correct index by relevant granularity
        indexOfSubBuffer /= m_deviceSimbPoolManager.getFactor(poolId);

        unsigned bufferSize =
            simbPoolContainerAllocator->getSimbSize(simbPoolContainerAllocator->getPoolContainerIndex(poolId));

        VERIFY(sizeInBytes <= bufferSize,
               "Unsupported buffer size, sizeInBytes={}, bufferSize={}",
               sizeInBytes,
               bufferSize);

        unsigned    memsetLoops = 1;
        hcclRedOp_t effectiveOp = sendSliceState.m_reduceOp;

        if (poolId == SCALEOUT_POOL || poolId == PRIMITIVE_POOL)
        {
            if (sendSliceState.m_16BitReduction)
            {
                sizeInBytes = sizeInBytes << 1;
            }
            effectiveOp = hcclSum;
        }

        LOG_TRACE(HCL_ECR,
                  "Clear buffer {}, loops {}, size 0x{:x}, long SO {}",
                  poolId,
                  memsetLoops,
                  sizeInBytes,
                  longSo.targetValue);

        uint32_t currNumberOfRanks;
        uint32_t currNumberOfSubBuffers;

        if (poolId == REDUCE_POOL)
        {
            VERIFY(recvSliceState.m_collectiveOp == eHCLReduce,
                   "REDUCE_POOL is only used in eHCLReduce collectiveOp, current collectiveOp={}",
                   recvSliceState.m_collectiveOp);
            // single chunk from each peer rank on recv / single chunk to cast down after reduce
            currNumberOfRanks = 1;
            // single buffer every slice
            currNumberOfSubBuffers = 1;
        }
        else if (poolId == SCALEOUT_POOL)
        {
            currNumberOfRanks      = std::min(sendSliceState.m_scaleoutBuffersAmount, sendSliceState.m_boxIterations);
            currNumberOfSubBuffers = sendSliceState.m_scaleoutBuffersAmount;
        }
        else if (poolId == PRIMITIVE_POOL)
        {
            currNumberOfRanks      = 1;
            currNumberOfSubBuffers = 1;
        }
        else
        {
            VERIFY(false, "The following pool id={} should not be used in memset.", poolId);
        }

        for (unsigned i = 0; i < memsetLoops; ++i)
        {
            m_commands.serializeMemsetCommand(garbageCollectionStream,
                                              schedIdx,
                                              sendSliceState.getIntermediateBuffer(poolId) +
                                                  i * bufferSize,  // for v3 commands, memsetLoops = 1, i = 0
                                              sizeInBytes,
                                              signalsManager->getSoAddress(WaitMethod::INTERNAL_CG_SO),
                                              streamCtxtID,
                                              dataType,
                                              effectiveOp,
                                              true,  // true for sibo memset v3, false for lin memset
                                              simbPoolContainerAllocator->getPoolContainerIndex(poolId),
                                              false,  // isForScaleout
                                              currNumberOfRanks,
                                              currNumberOfSubBuffers,
                                              indexOfSubBuffer);
        }
    }
}

void HclCollectiveMemHandlerGaudi2::enqueueInternalCompletionMemsetSignals(SignalsManager* signalsManager,
                                                                           e_devicePoolID  poolId)
{
    const unsigned memsetLoops = 1;
    if (m_deviceSimbPoolManager.bufferExpired(poolId))
    {
        for (unsigned i = 0; i < memsetLoops; ++i)
        {
            signalsManager->enqueueInternalCompletion(SignalEvent::EDMA_MEMSET);
        }
    }
}
