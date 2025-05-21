#include "platform/gen2_arch_common/hcl_mem_handler.h"
#include "platform/gen2_arch_common/hcl_packets_utils.h"

#include <cstdint>                                    // for uint64_t
#include "infra/scal/gen2_arch_common/scal_stream.h"  // for ScalStream
#include "hcl_log_manager.h"                          // for LOG_*
#include "hcl_utils.h"                                // for VERIFY
#include "hcl_math_utils.h"                           // for mod
#include "platform/gen2_arch_common/device_simb_pool_manager.h"
#include "simb_pool_container_allocator.h"
#include "platform/gen2_arch_common/hcl_graph_sync.h"         // for HclGraphSyncGen2Arch
#include "platform/gen2_arch_common/hcl_graph_sync.h"         // for HclGraphSyncGen2Arch
#include "platform/gen2_arch_common/hcl_address_generator.h"  // for HclAddressGenerator

HclCollectiveMemHandlerGen2Arch::HclCollectiveMemHandlerGen2Arch(int                        archStreamId,
                                                                 HclAddressGenerator&       addressGenerator,
                                                                 DeviceSimbPoolManagerBase& deviceSimbPoolManager,
                                                                 HclCommandsGen2Arch&       commands,
                                                                 HclGraphSyncGen2Arch&      graphSync)
: m_archStreamId(archStreamId),
  m_addressGenerator(addressGenerator),
  m_deviceSimbPoolManager(deviceSimbPoolManager),
  m_commands(commands),
  m_graphSync(graphSync)
{
    m_profilerDebugMode = GCFG_HCL_PROFILER_DEBUG_MODE.value();
}

HclCollectiveMemHandlerGen2Arch::~HclCollectiveMemHandlerGen2Arch() {}

void HclCollectiveMemHandlerGen2Arch::createMemCopyCommands(SimbPoolContainerAllocator* simbPoolContainerAllocator,
                                                            SliceState&                 sliceState,
                                                            SignalsManager*             signalsManager,
                                                            unsigned                    sliceIter,
                                                            BoxNumInfo&                 boxNumInfo,
                                                            uint64_t                    chunkCount,
                                                            hcl::ScalStream&            scalStream,
                                                            uint32_t                    dmaType,
                                                            bool                        isForScaleout,
                                                            e_devicePoolID              poolIdx,
                                                            bool                        isGDRMemcpy,
                                                            bool                        isForContReduction)

{
    uint64_t       copyCount        = chunkCount;
    uint32_t       soAddress        = 0;
    const bool     useSibo          = DeviceSimbPoolManagerBase::isSiboPool(poolIdx);
    const uint32_t indexOfSubBuffer = useSibo ? div(m_deviceSimbPoolManager.getSliceId(poolIdx, m_archStreamId),
                                                    DeviceSimbPoolManagerBase::getFactor(poolIdx))
                                              : 0;
    bool           isFirstBox       = boxNumInfo.m_boxNum == sliceState.m_dynamicComm.getMyScaleupGroup();
    SignalEvent    event =
        chooseMemCopyEvent(sliceState, dmaType, isFirstBox, isGDRMemcpy, useSibo, isForScaleout, isForContReduction);
    // If V3 and second signal if needed.
    if (isFirstBox && sliceState.m_currentOp == eHCLReduceScatter && sliceState.m_isMultiScaleupGroup &&
        sliceState.m_dynamicComm.getScaleupGroupSize() != 1 &&
        signalsManager->isEventRegistered(SignalEvent::SIGNAL_TO_LONGTERM))
    {
        soAddress = signalsManager->dequeueSoAddress(SignalEvent::SIGNAL_TO_LONGTERM);
    }

    uint64_t strideCount = sliceState.m_scaleUpStrideCount;
    uint64_t offset = sliceState.m_dynamicComm.getRankInScaleupGroup() * strideCount * sliceState.m_dataTypeSizeInBytes;
    uint64_t all2allDestOffset = sliceState.m_dynamicComm.getRankInScaleupGroup() *
                                 (isFirstBox ? strideCount : chunkCount) * sliceState.m_dataTypeSizeInBytes;

    uint64_t dstAddr = m_addressGenerator.generateMemcpyDstAddress(
        sliceState,
        sliceIter,
        boxNumInfo,
        sliceState.m_collectiveOp == eHCLAll2All ? all2allDestOffset : offset,
        isForScaleout,
        isGDRMemcpy,
        isForContReduction);

    uint64_t srcAddr =
        m_addressGenerator.generateMemcpySrcAddress(sliceState,
                                                    sliceIter,
                                                    boxNumInfo,
                                                    offset * (dmaType == DmaType::DMA_TYPE_CAST_DOWN ? 2 : 1),
                                                    isForScaleout,
                                                    isGDRMemcpy,
                                                    isForContReduction);

    LOG_HCL_CONTEXT_TRACE(HCL, "Serializing an edma command");

    unsigned numberOfRanks;
    unsigned numberOfSubBuffers = isForScaleout ? sliceState.m_scaleoutBuffersAmount : DEFAULT_BOX_SIZE;
    if (isForScaleout)
    {
        if (sliceState.isRSContReduction())
        {
            if (isForContReduction)
            {
                numberOfRanks = mod(sliceState.m_boxIter, sliceState.m_scaleoutBuffersAmount) + 1;
            }
            else  // Final reduction of Accumulating Buffer
            {
                numberOfRanks =
                    std::min(RS_CONT_REDUC_SO_POOL_AMOUNT,
                             static_cast<unsigned>(std::ceil(static_cast<float>(sliceState.m_boxIterations) /
                                                             sliceState.m_scaleoutBuffersAmount)));
                numberOfSubBuffers = RS_CONT_REDUC_SO_POOL_AMOUNT;
            }
        }
        else
        {
            numberOfRanks = std::min(sliceState.m_scaleoutBuffersAmount, sliceState.m_boxIterations);
        }
    }
    else
    {
        numberOfRanks = sliceState.m_dynamicComm.getScaleupGroupSize();
    }

    hcclDataType_t dataTypeForDma =
        ((sliceState.m_dataType == hcclBfloat16 || sliceState.m_dataType == hcclFloat16) && isForScaleout &&
         !(sliceState.isRSContReduction() && isForContReduction) && !isGDRMemcpy)
            ? hcclFloat32
            : sliceState.m_dataType;

    bool isWideAccumulation = sliceState.m_currentOp == eHCLReduceScatter && sliceState.isRSContReduction() &&
                              isForContReduction && sliceState.m_16BitReduction;  // casting elements before reduction
    bool useCasting = dmaType == DmaType::DMA_TYPE_CAST_DOWN || dmaType == DmaType::DMA_TYPE_CAST_UP;

    LOG_TRACE(HCL_ECR,
              "Counts for memcpy: op {}, box {}, slice {}, event {}, count {}, stride {}, "
              "src 0x{:X} dst 0x{:X}",
              sliceState.m_currentOp,
              boxNumInfo.m_boxNum,
              sliceIter,
              event,
              copyCount,
              strideCount,
              srcAddr,
              dstAddr);

    uint32_t poolContainerId = 0;
    if (poolIdx != NO_POOL)
    {
        poolContainerId = simbPoolContainerAllocator->getPoolContainerIndex(poolIdx);
    }

    const unsigned boxIter        = sliceState.calcBoxIterRecv(boxNumInfo);
    const uint8_t  edmaStreamCtxt = m_profilerDebugMode
                                        ? getEdmaDebugCtxtId(sliceState.m_apiId, isForScaleout, sliceIter)
                                        : getEdmaStreamCtxtId(sliceState.m_apiId, m_archStreamId);
    bool           isFirstWrite   = boxIter < DeviceSimbPoolManagerBase::getFactor(sliceState.m_execution.m_usedPool);
    if (sliceState.isRSContReduction() && isForContReduction)
    {
        isFirstWrite = sliceState.m_boxIter < (sliceState.m_scaleoutBuffersAmount * RS_CONT_REDUC_SO_POOL_AMOUNT);
    }

    // reduction op should be replaced to hcclSum if we're within the 1st use of the SCALEOUT buffer, to avoid issues
    // with min/max (as the buffer is set to 0 initially)
    bool replaceRedOp = isGDRMemcpy && isFirstWrite;

    DmaCmdParams cmd {scalStream.getSchedIdx(),
                      signalsManager->dequeueSoAddress(event),
                      soAddress,
                      sliceState.m_collectiveOp,
                      replaceRedOp ? hcclSum : sliceState.m_reduceOp,
                      edmaStreamCtxt,
                      copyCount,
                      strideCount,
                      dstAddr,
                      srcAddr,
                      dataTypeForDma,
                      sliceState.m_isReductionCollective,
                      useSibo,
                      numberOfRanks,
                      numberOfSubBuffers,
                      indexOfSubBuffer,
                      isForScaleout,
                      useCasting,
                      isGDRMemcpy,
                      poolContainerId,
                      sliceState.m_dataType == hcclBfloat16,
                      isFirstWrite,
                      isForContReduction,
                      isWideAccumulation};

    LOG_HCL_TRACE(HCL,
                  "Creating collective command SOAddressLSB(0x{:x}), SOAddressLSB2(0x{:x})",
                  cmd.m_soAddressLSB,
                  cmd.m_soAddressLSB2);
    m_commands.serializeDmaCommand(scalStream, cmd);
}

SignalEvent HclCollectiveMemHandlerGen2Arch::chooseMemCopyEvent(CommonState& commonState,
                                                                uint32_t     dmaType,
                                                                bool         isFirstBox,
                                                                bool         isGDRMemcpy,
                                                                bool         useSibo,
                                                                bool         isForScaleout,
                                                                bool         isForContReduction)
{
    SignalEvent event       = SignalEvent::SIGNAL_EVENT_MAX;
    bool        isPeersOnly = commonState.m_isMultiScaleupGroup && commonState.m_dynamicComm.getScaleupGroupSize() == 1;
    if (useSibo && !isForScaleout)
    {
        event = SignalEvent::EDMA_BATCH;
    }
    else if (useSibo && isForScaleout)
    {
        event = isForContReduction ? SignalEvent::EDMA_CONT_BATCH_SCALEOUT : SignalEvent::EDMA_BATCH_SCALEOUT;
    }
    else if (isGDRMemcpy && (!isPeersOnly || !isFirstBox))
    {
        event = SignalEvent::EDMA_MEMCOPY_GDR;
    }
    else if (!useSibo && isForScaleout && !isGDRMemcpy)
    {
        event = SignalEvent::EDMA_MEMCOPY_FOR_SCALEOUT;
    }
    else if (!useSibo && !isForScaleout)
    {
        event = dmaType == DmaType::DMA_TYPE_CAST_UP ? SignalEvent::EDMA_CAST_UP : SignalEvent::EDMA_MEMCOPY;
    }

    VERIFY(event != SignalEvent::SIGNAL_EVENT_MAX, "event is uninitialized!");

    return event;
}

void HclCollectiveMemHandlerGen2Arch::createMemCopyCommandsNonCollective(hcl::ScalStream& scalStream,
                                                                         HCL_Rank         myRank,
                                                                         uint64_t         chunkCount,
                                                                         hcclDataType_t   dataType,
                                                                         uint64_t         recvBaseAddress,
                                                                         uint64_t         sendBaseAddress,
                                                                         uint64_t         ScaleupGroupSize,
                                                                         uint8_t          apiId)
{
    LOG_HCL_TRACE(HCL,
                  "myRank={}, chunkCount={}, recvBaseAddress=0x{:x}, sendBaseAddress=0x{:x}, ScaleupGroupSize={}",
                  myRank,
                  chunkCount,
                  recvBaseAddress,
                  sendBaseAddress,
                  ScaleupGroupSize);
    uint32_t soAddress = m_graphSync.getCurrentCgSoAddr(CgType::eExternal);

    DmaCmdParams cmd {(unsigned)hcl::SchedulersIndex::sendScaleUp,
                      soAddress,
                      0,
                      eHCLNoCollective,
                      hcclOpNone,
                      getEdmaStreamCtxtId(apiId, m_archStreamId),
                      chunkCount,
                      chunkCount,
                      recvBaseAddress,
                      sendBaseAddress,
                      dataType,
                      false,
                      false,
                      0,
                      0,
                      0,
                      false,
                      false,
                      false,
                      0,
                      false,
                      false,
                      false};

    LOG_HCL_TRACE(HCL, "Creating non-collective command SOAddress(0x{:x})", soAddress);
    m_commands.serializeDmaCommand(scalStream, cmd);
}

void HclCollectiveMemHandlerGen2Arch::signalToSoViaEmptyDmaCommand(uint32_t         soAddress,
                                                                   hcl::ScalStream& scalStream,
                                                                   CommonState&     commonState)
{
    VERIFY(soAddress, "SO address is not set.");
    LOG_HCL_TRACE(HCL, "Signaling SOAddress(0x{:x})", soAddress);
    DmaCmdParams cmd {scalStream.getSchedIdx(),
                      soAddress,
                      0,
                      commonState.m_collectiveOp,
                      hcclSum,
                      getEdmaStreamCtxtId(commonState.m_apiId, m_archStreamId),
                      0,
                      0,
                      0,
                      0,
                      hcclFloat32,
                      0,
                      true,
                      1,
                      0,
                      0,
                      0,
                      0,
                      0,
                      1,
                      0,
                      0,
                      0};

    m_commands.serializeDmaCommand(scalStream, cmd);
}
