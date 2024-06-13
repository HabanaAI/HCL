#include "platform/gen2_arch_common/hcl_mem_handler.h"

#include <cstdint>                                            // for uint64_t
#include "infra/scal/gen2_arch_common/scal_stream.h"          // for ScalStream
#include "hcl_log_manager.h"                                  // for LOG_*
#include "platform/gen2_arch_common/commands/hcl_commands.h"  // for HclComm...
#include "hcl_utils.h"                                        // for VERIFY
#include "platform/gen2_arch_common/device_buffer_manager.h"
#include "intermediate_buffer_container.h"
#include "platform/gen2_arch_common/hcl_graph_sync.h"         // for HclGraphSyncGen2Arch
#include "platform/gen2_arch_common/signals/manager.h"        // for SignalsManager
#include "platform/gen2_arch_common/hcl_graph_sync.h"         // for HclGraphSyncGen2Arch
#include "platform/gen2_arch_common/hcl_address_generator.h"  // for HclAddressGenerator

HclCollectiveMemHandlerGen2Arch::HclCollectiveMemHandlerGen2Arch(int                   archStreamId,
                                                                 HclAddressGenerator&  addressGenerator,
                                                                 DeviceBufferManager&  intermediateBufferManager,
                                                                 HclCommandsGen2Arch&  commands,
                                                                 HclGraphSyncGen2Arch& graphSync)
: m_archStreamId(archStreamId),
  m_addressGenerator(addressGenerator),
  m_intermediateBufferManager(intermediateBufferManager),
  m_commands(commands),
  m_graphSync(graphSync)
{
}

HclCollectiveMemHandlerGen2Arch::~HclCollectiveMemHandlerGen2Arch() {}

void HclCollectiveMemHandlerGen2Arch::createMemCopyCommands(CommonState&     commonState,
                                                            SignalsManager*  signalsManager,
                                                            unsigned         sliceIter,
                                                            BoxNumInfo&      boxNumInfo,
                                                            uint64_t         chunkCount,
                                                            hcl::ScalStream& scalStream,
                                                            uint32_t         dmaType,
                                                            bool             reductionSignalToCg,
                                                            uint32_t         indexOfReproBuffer,
                                                            bool             useSibo,
                                                            bool             isForScaleout,
                                                            bool             isRRLast,
                                                            e_devicePoolID   poolIdx,
                                                            bool             isReductionStream)
{
    uint64_t copyCount                 = chunkCount;
    uint32_t soAddress                 = 0;
    bool     reductionIsFirstBoxMemcpy = commonState.m_isMultiPod && commonState.m_isReductionCollective &&
                                     boxNumInfo.m_boxNum == commonState.m_dynamicComm.getMyPod();

    SignalEvent event = chooseMemCopyEvent(commonState, dmaType, boxNumInfo, useSibo, isForScaleout, isRRLast);
    if (dmaType == m_commands.getDmaTypeCastDown() && chunkCount != 0 && !GCFG_HCL_USE_EDMA_COMMAND_V3.value())
    {
        soAddress = signalsManager->enqueueInternalCompletion(event);  // memset SO for new commands - cdc
    }

    // If V3 and second signal if needed.
    bool isFirstBox = boxNumInfo.m_boxNum == commonState.m_dynamicComm.getMyPod();
    if (GCFG_HCL_USE_EDMA_COMMAND_V3.value() && isFirstBox && commonState.m_currentOp == eHCLReduceScatter &&
        commonState.m_isMultiPod && commonState.m_dynamicComm.getPodSize() != 1)
    {
        soAddress = signalsManager->dequeueSoAddress(SignalEvent::RR_SIGNAL_TO_LONGTERM);
    }

    // In case of no-work for a device, will not send a command to the scheduler, we will signal instead. (signals are
    // counted for this case!)
    if (chunkCount == 0 && !GCFG_HCL_USE_EDMA_COMMAND_V3.value())
    {
        m_commands.serializeLbwWriteCommand(scalStream,
                                            scalStream.getSchedIdx(),
                                            signalsManager->dequeueSoAddress(event),
                                            m_graphSync.getSoConfigValue(commonState.signalToCost(event), true));
        return;
    }

    uint64_t strideCount = commonState.m_scaleUpStrideCount;
    uint64_t offset      = commonState.m_dynamicComm.getRankInPod() * strideCount * commonState.m_dataTypeSizeInBytes;
    uint64_t all2allDestOffset =
        commonState.m_dynamicComm.getRankInPod() *
        (boxNumInfo.m_boxNum == commonState.m_dynamicComm.getMyPod() ? strideCount : chunkCount) *
        commonState.m_dataTypeSizeInBytes;

    bool     isLocalMemcpy = !useSibo && !isRRLast;
    bool     isPeersOnly   = commonState.m_isMultiPod && commonState.m_dynamicComm.getPodSize() == 1;
    bool     isGDRMemcpy   = event == SignalEvent::EDMA_MEMCOPY_GDR;
    uint64_t dstAddr       = m_addressGenerator.generateMemcpyDstAddress(
        commonState,
        sliceIter,
        boxNumInfo,
        reductionSignalToCg || (commonState.m_isReductionCollective && !isLocalMemcpy),
        dmaType,
        commonState.m_collectiveOp == eHCLAll2All ? all2allDestOffset : offset,
        reductionIsFirstBoxMemcpy,
        (commonState.m_isReductionCollective && isLocalMemcpy && !isPeersOnly) ||
            useSibo,  // regular memcpy in RR mode (not in place, first memcpy)
        useSibo,
        isRRLast,
        isForScaleout,
        isReductionStream,
        isGDRMemcpy);

    uint64_t srcAddr = m_addressGenerator.generateMemcpySrcAddress(
        commonState,
        sliceIter,
        boxNumInfo,
        reductionSignalToCg,
        dmaType,
        offset * (m_commands.isCastDown(dmaType) ? 2 : 1),
        commonState.m_isReductionCollective && !isLocalMemcpy,  // calculation of base address for batch mode memcpy
        useSibo,
        isRRLast,
        isForScaleout,
        isReductionStream,
        isGDRMemcpy);

    LOG_HCL_CONTEXT_TRACE(HCL, "Serializing an edma command");

    unsigned numberOfRanks        = isForScaleout
                                        ? std::min(commonState.m_reproScaleoutBuffersAmount, commonState.m_boxIterations)
                                        : commonState.m_dynamicComm.getPodSize();
    unsigned numberOfReproBuffers = isForScaleout ? commonState.m_reproScaleoutBuffersAmount : DEFAULT_BOX_SIZE;

    hcclDataType_t dataTypeForDma =
        ((commonState.m_dataType == hcclBfloat16 || commonState.m_dataType == hcclFloat16) && isForScaleout && useSibo)
            ? hcclFloat32
            : commonState.m_dataType;

    bool useCasting =
        (dmaType == m_commands.getDmaTypeCastDown() ||
         (dmaType == m_commands.getDmaTypeCastUp() && boxNumInfo.m_boxNum == commonState.m_dynamicComm.getMyPod()) ||
         (isGDRMemcpy && m_commands.getDmaTypeCastUp()));

    LOG_TRACE(HCL_ECR,
              "Counts for memcpy: op {}, box {}, slice {}, event {}, count {}, stride {}, "
              "src 0x{:X} dst 0x{:X}",
              commonState.m_currentOp,
              boxNumInfo.m_boxNum,
              sliceIter,
              event,
              copyCount,
              strideCount,
              srcAddr,
              dstAddr);

    uint32_t poolId = 0;
    if (GCFG_HCL_USE_EDMA_COMMAND_V3.value())
    {
        if (poolIdx != NO_POOL)
        {
            poolId = DeviceBufferManager::getPoolSizeIndex(poolIdx);
        }
    }
    else if ((commonState.m_isReductionCollective && !isLocalMemcpy) || isGDRMemcpy)
    {
        poolId = m_intermediateBufferManager.getPoolSizeIndexByAddr(srcAddr);
    }

    const unsigned boxIter = commonState.calcBoxIterRecv(boxNumInfo);
    // reduction op should be replaced to hcclSum if we're within the 1st use of the SCALEOUT_RR buffer, to avoid issues
    // with min/max (as the buffer is set to 0 initially)
    bool replaceRedOp = isGDRMemcpy && boxIter < commonState.m_reproScaleoutBuffersAmount;

    DmaCmdParams cmd {scalStream.getSchedIdx(),
                      dmaType,
                      signalsManager->dequeueSoAddress(event),
                      soAddress,
                      commonState.m_collectiveOp,
                      replaceRedOp ? hcclSum : commonState.m_reduceOp,
                      hcl::encodeStreamContextID(commonState.m_apiId, m_archStreamId),
                      copyCount,
                      strideCount,
                      dstAddr,
                      srcAddr,
                      dataTypeForDma,
                      reductionSignalToCg,
                      commonState.m_isReductionCollective,
                      useSibo,
                      numberOfRanks,
                      numberOfReproBuffers,
                      indexOfReproBuffer,
                      isForScaleout,
                      useCasting,
                      isGDRMemcpy,
                      poolId,
                      commonState.m_dataType == hcclBfloat16};

    LOG_HCL_TRACE(HCL,
                  "Creating collective command SOAddressLSB(0x{:x}), SOAddressLSB2(0x{:x})",
                  cmd.m_soAddressLSB,
                  cmd.m_soAddressLSB2);
    m_commands.serializeDmaCommand(scalStream, cmd);
}

SignalEvent HclCollectiveMemHandlerGen2Arch::chooseMemCopyEvent(CommonState& commonState,
                                                                uint32_t     dmaType,
                                                                BoxNumInfo&  boxNumInfo,
                                                                bool         useSibo,
                                                                bool         isForScaleout,
                                                                bool         isRRLast)
{
    SignalEvent event       = SignalEvent::SIGNAL_EVENT_MAX;
    bool        isPeersOnly = commonState.m_isMultiPod && commonState.m_dynamicComm.getPodSize() == 1;
    if (dmaType == m_commands.getDmaTypeMemCpy())
    {
        if (useSibo && !isForScaleout)
        {
            event = SignalEvent::EDMA_BATCH;
        }
        else if (useSibo && isForScaleout)
        {
            event = SignalEvent::EDMA_BATCH_SCALEOUT;
        }
        else if (isRRLast && !isForScaleout && commonState.m_isReductionCollective)
        {
            event = SignalEvent::EDMA_MEMCOPY_RR;
        }
        else if (isRRLast && isForScaleout && commonState.m_isReductionCollective)
        {
            event = SignalEvent::EDMA_MEMCOPY_RR_LAST_BOX;
        }
        else if (isForScaleout && !commonState.m_isReductionCollective)
        {
            event = SignalEvent::EDMA_MEMCOPY_FOR_SCALEOUT;
        }
        else if (!useSibo && !isRRLast && isForScaleout && commonState.m_isGdr &&
                 (!isPeersOnly || boxNumInfo.m_boxNum != commonState.m_dynamicComm.getMyPod()))
        {
            event = SignalEvent::EDMA_MEMCOPY_GDR;
        }
        else
        {
            event = SignalEvent::EDMA_MEMCOPY;
        }
    }
    if (dmaType == m_commands.getDmaTypeCastUp())
    {
        if (useSibo)
        {
            event = SignalEvent::EDMA_BATCH;
        }
        else if (isRRLast)
        {
            event = SignalEvent::EDMA_MEMCOPY_RR;
        }
        else if (!useSibo && !isRRLast && isForScaleout && commonState.m_isGdr &&
                 (!isPeersOnly || boxNumInfo.m_boxNum != commonState.m_dynamicComm.getMyPod()))
        {
            event = SignalEvent::EDMA_MEMCOPY_GDR;
        }
        else
        {
            event = SignalEvent::EDMA_CAST_UP;
        }
    }
    if (dmaType == m_commands.getDmaTypeCastDown())
    {
        if (isRRLast)
        {
            event = SignalEvent::EDMA_MEMCOPY_RR_LAST_BOX;
        }
        else if (GCFG_HCL_USE_EDMA_COMMAND_V3.value())
        {
            event = SignalEvent::EDMA_BATCH_SCALEOUT;
        }
        else if (isForScaleout)
        {
            event = SignalEvent::EDMA_CAST_DOWN_FOR_SCALEOUT;
        }
        else
        {
            event = SignalEvent::EDMA_CAST_DOWN;
        }
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
                                                                         uint64_t         podSize,
                                                                         uint8_t          apiId)
{
    LOG_HCL_TRACE(HCL,
                  "myRank={}, chunkCount={}, recvBaseAddress=0x{:x}, sendBaseAddress=0x{:x}, podSize={}",
                  myRank,
                  chunkCount,
                  recvBaseAddress,
                  sendBaseAddress,
                  podSize);
    uint32_t soAddress = m_graphSync.getCurrentCgSoAddr(CgType::eExternal);

    DmaCmdParams cmd {(unsigned)hcl::SchedulersIndex::sendScaleUp,
                      m_commands.getDmaTypeMemCpy(),
                      soAddress,
                      0,
                      eHCLNoCollective,
                      hcclOpNone,
                      hcl::encodeStreamContextID(apiId, m_archStreamId),
                      chunkCount,
                      chunkCount,
                      recvBaseAddress,
                      sendBaseAddress,
                      dataType,
                      false,
                      false,
                      false,
                      0,
                      0,
                      0,
                      false,
                      false,
                      false,
                      0,
                      false};

    LOG_HCL_TRACE(HCL, "Creating non-collecive command SOAddress(0x{:x})", soAddress);
    m_commands.serializeDmaCommand(scalStream, cmd);
}

void HclCollectiveMemHandlerGen2Arch::signalToSoViaEmptyDmaCommand(uint32_t         soAddress,
                                                                   hcl::ScalStream& scalStream,
                                                                   CommonState&     commonState)
{
    VERIFY(soAddress, "SO address is not set.");
    LOG_HCL_TRACE(HCL, "Signaling SOAddress(0x{:x})", soAddress);
    DmaCmdParams cmd {scalStream.getSchedIdx(),
                      m_commands.getDmaTypeMemCpy(),
                      soAddress,
                      0,
                      commonState.m_collectiveOp,
                      hcclSum,
                      hcl::encodeStreamContextID(commonState.m_apiId, m_archStreamId),
                      0,
                      0,
                      0,
                      0,
                      hcclFloat32,
                      0,
                      0,
                      true,
                      1,
                      0,
                      0,
                      0,
                      0,
                      0,
                      1,
                      0};

    m_commands.serializeDmaCommand(scalStream, cmd);
}