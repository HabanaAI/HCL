#include "platform/gaudi2/hcl_mem_handler.h"
#include "platform/gen2_arch_common/hcl_mem_handler.h"
#include "platform/gen2_arch_common/hcl_device_controller.h"
#include "platform/gen2_arch_common/intermediate_buffer_container.h"

HclCollectiveMemHandlerGaudi2::HclCollectiveMemHandlerGaudi2(int                   archStreamId,
                                                             HclAddressGenerator&  addressGenerator,
                                                             DeviceBufferManager&  intermediateBufferManager,
                                                             HclCommandsGen2Arch&  commands,
                                                             HclGraphSyncGen2Arch& graphSync)
: HclCollectiveMemHandlerGen2Arch(archStreamId, addressGenerator, intermediateBufferManager, commands, graphSync)
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
                       DeviceBufferManager::getFactor(SCALEUP_AND_ALL2ALL_POOL);
        LOG_HCL_TRACE(HCL, "Setting scale-up receive index to {}", subBuffIndex);
    }
}

void HclCollectiveMemHandlerGaudi2::memsetIMBs(hcl::IntermediateBufferContainer* imbContainer,
                                               SignalsManager*                   signalsManager,
                                               SliceState&                       sendSliceState,
                                               SliceState&                       recvSliceState,
                                               unsigned int                      sizeInBytes,
                                               hcl::syncInfo                     longSo,
                                               unsigned                          schedIdx,
                                               hcl::ScalStream&                  garbageCollectionStream,
                                               HCL_StreamId                      m_streamId,
                                               e_devicePoolID                    poolId,
                                               uint8_t                           streamCtxtID,
                                               hcclDataType_t                    dataType)
{
    // get relevant slice
    unsigned indexOfSubBuffer = m_intermediateBufferManager.getSliceId(poolId, m_streamId);

    // get correct index by relevant granularity
    indexOfSubBuffer /= m_intermediateBufferManager.getFactor(poolId);

    if (m_intermediateBufferManager.bufferExpired(poolId))
    {
        unsigned bufferSize = imbContainer->getSliceSize(DeviceBufferManager::getPoolSizeIndex(poolId));

        VERIFY(sizeInBytes <= bufferSize,
               "Unsupported buffer size, sizeInBytes={}, bufferSize={}",
               sizeInBytes,
               bufferSize);

        unsigned    memsetLoops   = 1;
        unsigned    initialOffset = 0;
        hcclRedOp_t effectiveOp   = sendSliceState.m_reduceOp;

        if (poolId == SCALEOUT_POOL)
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
        else
        {
            VERIFY(false, "The following pool id={} should not be used in memset.", poolId);
        }

        for (unsigned i = 0; i < memsetLoops; ++i)
        {
            m_commands.serializeMemsetCommand(garbageCollectionStream,
                                              schedIdx,
                                              sendSliceState.getIntermediateBuffer(poolId) + initialOffset +
                                                  i * bufferSize,  // for v3 commands, memsetLoops = 1, i = 0
                                              sizeInBytes,
                                              signalsManager->enqueueInternalCompletion(SignalEvent::EDMA_MEMSET),
                                              streamCtxtID,
                                              dataType,
                                              effectiveOp,
                                              true,  // true for sibo memset v3, false for lin memset
                                              poolId,
                                              false,  // isForScaleout
                                              currNumberOfRanks,
                                              currNumberOfSubBuffers,
                                              indexOfSubBuffer);
        }
    }
}
