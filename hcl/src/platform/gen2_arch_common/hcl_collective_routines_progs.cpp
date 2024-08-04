#include "platform/gen2_arch_common/hcl_collective_routines.h"

#include <ext/alloc_traits.h>  // for __alloc...
#include <cstdint>             // for uint64_t
#include <string>              // for string

#include "hcl_log_manager.h"                                  // for LOG_*
#include "platform/gen2_arch_common/commands/hcl_commands.h"  // for HclComm...
#include "platform/gen2_arch_common/hcl_device.h"             // for HclDevi...
#include "platform/gen2_arch_common/signals/types.h"
#include "platform/gen2_arch_common/collective_states.h"
#include "hcl_utils.h"  // for VERIFY
#include "platform/gen2_arch_common/device_buffer_manager.h"
#include "platform/gen2_arch_common/scaleout_provider.h"  // for ScaleoutProvider
#include "platform/gen2_arch_common/descriptors.h"
#include "platform/gen2_arch_common/host_buffer_manager.h"  // for HostBufferManager
#include "platform/gen2_arch_common/hcl_graph_sync.h"       // for HclGraphSyncGen2Arch
#include "platform/gen2_arch_common/wqe_tracker.h"          // for QpType, WqeTracker
#include "platform/gen2_arch_common/signals/manager.h"      // for SignalsManager
#include "platform/gen2_arch_common/dependency_checker.h"   // for DependencyChecker
#include "platform/gen2_arch_common/collective_utils.h"     // for getNextBox, getPrevBox
#include "platform/gen2_arch_common/active_stream_manager.h"
#include "hcl_math_utils.h"

static std::map<uint32_t, std::string> g_signalNames;

void HclCollectiveRoutinesGen2Arch::initCollectiveRoutinesGen2Arch()
{
    LOG_HCL_TRACE(HCL, "Initializing DeviceController");
    m_deviceController.initDeviceForCollectiveRoutine(m_streamId, &m_longSo, &m_longSoNullSubmit);

    m_signalsManager = new SignalsManager(m_graphSync,
                                          m_utils,
                                          m_graphSync.getCgData(true).size,
                                          m_streamId);

    m_deviceController.setSignalFinalize(m_streamId, [&]() { m_signalsManager->finalize(false); });

    m_dependencyChecker = std::make_unique<DependencyChecker>(m_graphSync.getCgData(false).size);

    m_deviceController.submitWork(m_streamId);
}

void HclCollectiveRoutinesGen2Arch::createDmaProgs(SliceState&    sendSliceState,
                                                   SliceState&    recvSliceState,
                                                   unsigned int   sizeInBytes,
                                                   unsigned       requiredCredits,
                                                   hcclDataType_t dataType)
{
    unsigned         sliceIter = sendSliceState.m_sliceIter;
    unsigned         sendBoxNum = sendSliceState.m_boxNumInfo.m_boxNum;
    unsigned         schedIdx  = (unsigned)hcl::SchedulersIndex::dma;

    // No need to cast down self box, as there's no need to send it to other boxes.
    bool isFirstBox = sendBoxNum == sendSliceState.m_dynamicComm.getMyScaleupGroup();

    ActiveStreamManagerGen2Arch activeStreamManager(sendSliceState,
                                                    m_scaleoutProvider,
                                                    m_deviceController,
                                                    m_streamId,
                                                    schedIdx);

    activeStreamManager.setTargetValueForAllDmaStreams(m_longSo.targetValue);

    hcl::ScalStream* arbitratorStream = activeStreamManager.getDmaScalStream(hcl::DMAStreams::arbitrator);
    m_deviceController.addBarrierArm(*arbitratorStream,
                                     false,
                                     requiredCredits,
                                     activeStreamManager.getActiveDmaStreams());

    uint64_t chunkCountForActivateReductionStream = 0;

    hcl::ScalStream* currentStream = nullptr;

    if ((currentStream = activeStreamManager.getDmaScalStream(hcl::DMAStreams::reduction)) != nullptr)
    {
        LOG_HCL_CONTEXT_TRACE(HCL, "Running dma for scaleup");

        m_deviceController.waitForBarrierArm(*currentStream);

        streamAddSingleWaitIfNeeded(*currentStream,
                                    {WaitEvent::RR_DMA_WAIT_FOR_RECV,
                                     WaitEvent::RR_DMA_WAIT_FOR_SU_RECV,
                                     WaitEvent::RR_DMA_BATCH_WAIT_FOR_SCALEOUT_RECV});

        chunkCountForActivateReductionStream = sendSliceState.m_rankScaleOutCount;

        uint32_t indexOfReproBuffer = 0;

        e_devicePoolID poolIdx = SCALEUP_RR_AND_ALL2ALL_POOL;
        indexOfReproBuffer =
            m_intermediateBufferManager.getSliceId(poolIdx, m_streamId) / (RR_BUFFER_GRANULARITY_SCALEUP);
        bool     shouldCastUp     = sendSliceState.m_16BitReduction && sendSliceState.m_isMultiScaleupGroup;
        bool     isLastReduceRoot = (sendSliceState.m_collectiveOp == eHCLReduce &&
                                 !(sendSliceState.m_isMultiScaleupGroup && sendSliceState.m_16BitReduction));
        uint32_t dmaType          = (isLastReduceRoot && sendSliceState.m_16BitReduction &&
                                     sendSliceState.m_isMultiScaleupGroup) ? m_commands.getDmaTypeCastDown()
                                        : (shouldCastUp ? m_commands.getDmaTypeCastUp()
                                                        : m_commands.getDmaTypeMemCpy());

        uint32_t soLtuAddress = 0;
        if (isFirstBox && sendSliceState.m_dynamicComm.getScaleupGroupSize() != 1 &&
            sendSliceState.m_syncUpBufferWithLtu)
        {
            unsigned upBufferIdx = m_intermediateBufferManager.getCurrentBufferIdx(SCALEUP_RR_AND_ALL2ALL_POOL);
            soLtuAddress         = m_graphSync.getCurrentLtuGpsoAddr(upBufferIdx);
            unsigned lastVal     = m_graphSync.getCurrentLtuGpsoData(upBufferIdx);
            unsigned nextVal     = m_graphSync.getCurrentLtuGpsoData(
                upBufferIdx,
                sendSliceState.m_workDistributionGroupSize);  // edma will signal multiple times

            if (nextVal < lastVal)
            {
                // we encounterd an overflow, EDMA signals only increment SO value and can't set it.
                // 1) update the expected LTU SO Value after edma signals
                // 2) use LBW write to set SO value to zero
                // 3) add wait to make sure SO is set to zero before continueing to prevent a race.
                // * we don't mind the proformance since in the worst case it happnes every ~32K iterations.
                m_graphSync.getCurrentLtuGpsoData(upBufferIdx, SO_MAX_VAL - lastVal);
                m_commands.serializeLbwWriteCommand(*currentStream,
                                                    currentStream->getSchedIdx(),
                                                    soLtuAddress,
                                                    m_graphSync.getSoConfigValue(0, false));
                m_deviceController.addInternalWait(*currentStream, 0, m_graphSync.getCurrentLtuGpsoIdx(upBufferIdx));
            }
        }

        // batch mode COMMAND
        m_memHandler->createMemCopyCommands(sendSliceState,
                                            m_signalsManager,
                                            sliceIter,
                                            sendSliceState.m_boxNumInfo,
                                            chunkCountForActivateReductionStream,
                                            *currentStream,
                                            dmaType,
                                            false,
                                            indexOfReproBuffer,
                                            true,
                                            false,
                                            false,
                                            poolIdx,
                                            true);

        // Wait for 1 additional signal
        streamAddSingleWaitIfNeeded(
            *currentStream,
            {WaitEvent::RR_FINAL_DMA_WAIT_FOR_EDMA, WaitEvent::RR_REDUCE_FINAL_SCALEOUT_DMA_WAIT_FOR_DMA_BATCH});

        if (soLtuAddress)
        {
            m_memHandler->signalToSoViaEmptyDmaCommand(soLtuAddress, *currentStream, sendSliceState);
        }
    }

    if ((currentStream = activeStreamManager.getDmaScalStream(hcl::DMAStreams::gdr)) != nullptr)
    {
        LOG_HCL_CONTEXT_TRACE(HCL, "Running dma for gaudi-direct");
        m_deviceController.waitForBarrierArm(*currentStream);

        streamAddSingleWaitIfNeeded(*currentStream, {WaitEvent::GDR_MEMCPY_WAIT_FOR_HNIC_RECV});

        // Copy from GDR buffer to SO_RR buffer
        m_memHandler->createMemCopyCommands(sendSliceState,
                                            m_signalsManager,
                                            sliceIter,
                                            recvSliceState.m_boxNumInfo,
                                            recvSliceState.m_rankScaleOutCount,
                                            *currentStream,
                                            (sendSliceState.m_16BitReduction) ? m_commands.getDmaTypeCastUp()
                                                                              : m_commands.getDmaTypeMemCpy(),
                                            false,
                                            0, /* indexOfReproBuffer */
                                            false,
                                            /*isForScaleout=*/true,
                                            false,
                                            SCALEOUT_GDR_POOL);

        streamAddSingleWaitIfNeeded(*currentStream, {WaitEvent::HNIC_SIGNAL_SPLIT_WAIT_FOR_GDR_MEMCPY});

        LBWBurstDestData_t destData;
        destData.push_back(
            {m_signalsManager->dequeueSoAddress(SignalEvent::RR_SIGNAL_TO_LONGTERM),
             m_graphSync.getSoConfigValue(sendSliceState.signalToCost(SignalEvent::RR_SIGNAL_TO_LONGTERM), true)});
        destData.push_back(
            {m_signalsManager->dequeueSoAddress(SignalEvent::RR_SIGNAL_TO_CG),
             m_graphSync.getSoConfigValue(sendSliceState.signalToCost(SignalEvent::RR_SIGNAL_TO_CG), true)});
        m_commands.serializeLbwBurstWriteCommand(*currentStream, currentStream->getSchedIdx(), destData);
    }

    if ((currentStream = activeStreamManager.getDmaScalStream(hcl::DMAStreams::signaling)) != nullptr)
    {
        m_deviceController.waitForBarrierArm(*currentStream);

        if (!isFirstBox && m_scaleoutProvider->isHostNic())
        {
            VERIFY(!m_scaleoutProvider->isGaudiDirect(), "Signaling stream shouldn't be used with GDR");
            streamAddSingleWaitIfNeeded(*currentStream, {WaitEvent::HNIC_SIGNAL_SPLIT_WAIT_FOR_PDMA});
            LBWBurstDestData_t destData;
            destData.push_back(
                {m_signalsManager->dequeueSoAddress(SignalEvent::RR_SIGNAL_TO_LONGTERM),
                 m_graphSync.getSoConfigValue(sendSliceState.signalToCost(SignalEvent::RR_SIGNAL_TO_LONGTERM), true)});
            destData.push_back(
                {m_signalsManager->dequeueSoAddress(SignalEvent::RR_SIGNAL_TO_CG),
                 m_graphSync.getSoConfigValue(sendSliceState.signalToCost(SignalEvent::RR_SIGNAL_TO_CG), true)});
            m_commands.serializeLbwBurstWriteCommand(*currentStream, currentStream->getSchedIdx(), destData);
        }

        if (sendSliceState.m_syncUpBufferWithLtu && !isFirstBox)
        {
            LBWBurstDestData_t destData;
            streamAddSingleWaitIfNeeded(*currentStream,
                                        {WaitEvent::RR_FIRST_BOX_FINAL_SIGNAL_WAIT_FOR_GPSO,
                                         WaitEvent::RR_LTU_SIGNALING_WAIT_FOR_SCALEOUT_SEND});

            unsigned upBufferIdx = m_intermediateBufferManager.getCurrentBufferIdx(SCALEUP_RR_AND_ALL2ALL_POOL);
            destData.push_back(
                {m_graphSync.getCurrentLtuGpsoAddr(upBufferIdx),
                 m_graphSync.getSoConfigValue(m_graphSync.getCurrentLtuGpsoData(upBufferIdx, true), false)});
            destData.push_back(
                {m_signalsManager->dequeueSoAddress(SignalEvent::RR_SIGNAL_TO_CG),
                 m_graphSync.getSoConfigValue(sendSliceState.signalToCost(SignalEvent::RR_SIGNAL_TO_CG), true)});
            m_commands.serializeLbwBurstWriteCommand(*currentStream, currentStream->getSchedIdx(), destData);
        }
    }

    uint64_t chunkCountForActivateScaleoutReductionStream = 0;

    if ((currentStream = activeStreamManager.getDmaScalStream(hcl::DMAStreams::scaleoutReduction)) != nullptr)
    {
        LOG_HCL_CONTEXT_TRACE(HCL, "Running dma for scaleout");

        m_deviceController.waitForBarrierArm(*currentStream);

        streamAddSingleWaitIfNeeded(*currentStream,
                                    {WaitEvent::RR_RS_SO_WAIT_FOR_ALL_RECV,
                                     WaitEvent::RR_DMA_BATCH_WAIT_FOR_SCALEOUT_RECV,
                                     WaitEvent::RR_DMA_BATCH_WAIT_FOR_GDR_MEMCPY});

        chunkCountForActivateScaleoutReductionStream = recvSliceState.m_rankScaleOutCount;

        unsigned indexOfReproBuffer = m_intermediateBufferManager.getSliceId(SCALEOUT_RR_POOL, m_streamId);

        indexOfReproBuffer /= RR_BUFFER_GRANULARITY_SCALEOUT;

        m_memHandler->createMemCopyCommands(sendSliceState,
                                            m_signalsManager,
                                            sliceIter,
                                            sendSliceState.m_boxNumInfo,
                                            chunkCountForActivateScaleoutReductionStream,
                                            *currentStream,
                                            sendSliceState.m_16BitReduction ? m_commands.getDmaTypeCastDown()
                                                                            : m_commands.getDmaTypeMemCpy(),
                                            false,
                                            indexOfReproBuffer,
                                            true,
                                            /*isForScaleout=*/true,
                                            false,
                                            SCALEOUT_RR_POOL);

        streamAddSingleWaitIfNeeded(*currentStream, {WaitEvent::RR_FINAL_SCALEOUT_DMA_WAIT_FOR_DMA_BATCH});
    }

    hcl::ScalStream* garbageStream = activeStreamManager.getDmaScalStream(hcl::DMAStreams::garbageCollection);

    m_deviceController.waitForBarrierArm(*garbageStream);
    m_deviceController.addBarrierArm(*garbageStream, true, 1, {});

    {
        const auto& methodsToClean            = m_signalsManager->getMethodsToClean();
        bool        expiringByLongtermManager =
            m_deviceController.getSyncParams(m_streamId).m_longtermGPSOManager->isCreditExpiring();
        bool        expiringByGraph =
            methodsToClean[(unsigned)WaitMethod::GPSO_LONGTERM + recvSliceState.m_reproScaleoutLongtermAmount - 1];

        VERIFY(expiringByLongtermManager == expiringByGraph,
               "Longterm GPSO credit ({}) is expiring at a different time than graph thinks ({}) at credit {}",
               expiringByLongtermManager,
               expiringByGraph,
               m_longSo.targetValue);

        m_graphSync.createResetSoMessages(*garbageStream,
                                          garbageStream->getSchedIdx(),
                                          m_deviceController.getSyncParams(m_streamId).m_smInfo.soSmIndex,
                                          methodsToClean);
    }

    if (m_graphSync.isForceOrder(false))
    {
        m_signalsManager->enqueueInternalCompletion(SignalEvent::FORCE_ORDER);
    }

    for (auto buffer_pool : m_memset_buffers)
    {
        m_memHandler->memsetIMBs(m_device->m_sibContainer,
                                 m_signalsManager,
                                 sendSliceState,
                                 recvSliceState,
                                 sizeInBytes,
                                 m_longSo,
                                 garbageStream->getSchedIdx(),
                                 *garbageStream,
                                 m_streamId,
                                 buffer_pool,
                                 hcl::encodeStreamContextID(sendSliceState.m_apiId, m_streamId),
                                 dataType);
    }

    LOG_HCL_TRACE(HCL,
                  "using {} internal signals for cleanup (post-collective) on {}",
                  m_signalsManager->getNumSignalsForInternal(),
                  m_utils->printSOBInfo(m_signalsManager->getSoAddress(WaitMethod::INTERNAL_CG_SO)));
    m_commands.serializeLbwWriteCommand(
        *garbageStream,
        garbageStream->getSchedIdx(),
        m_signalsManager->getSoAddress(WaitMethod::INTERNAL_CG_SO),
        m_graphSync.getSoConfigValue(COMP_SYNC_GROUP_CMAX_TARGET - m_signalsManager->getNumSignalsForInternal(), true));
    m_deviceController.incInternalCgTargetValue(m_streamId);
}

void HclCollectiveRoutinesGen2Arch::createScaleUpSendProgsNonCollective(
    uint32_t                                 numberOfSendBuckets,
    uint32_t                                 numberOfRecvBuckets,
    uint32_t                                 numberOfSends,
    uint32_t                                 numberOfRecvs,
    const SendRecvArray&                     sendVec,
    const std::vector<SendRecvMemCopyEntry>& memcopyVec,
    unsigned                                 scaleoutSignals,
    HCL_Comm                                 comm,
    unsigned                                 requiredCredits,
    uint8_t                                  apiId)
{
    LOG_HCL_TRACE(HCL,
                  "numberOfSendBuckets={}, numberOfRecvBuckets={}, numberOfSends={}, numberOfRecvs={}, "
                  "scaleoutSignals={}, requiredCredits={}, memcopyVec.size={}, sendVec={}",
                  numberOfSendBuckets,
                  numberOfRecvBuckets,
                  numberOfSends,
                  numberOfRecvs,
                  scaleoutSignals,
                  requiredCredits,
                  memcopyVec.size(),
                  sendVec);
    hcl::ScalStream& currentStream =
        ActiveStreamManagerGen2Arch::getActiveCollectiveStream(m_deviceController,
                                                               eHCLNoCollective,
                                                               m_streamId,
                                                               (unsigned)hcl::SchedulersIndex::sendScaleUp);
    currentStream.setTargetValue(m_longSo.targetValue);

    hcl::ScalStream& arbitratorStream =
        m_deviceController.getScalStream(m_streamId, currentStream.getSchedIdx(), ARB_STREAM_IDX);
    arbitratorStream.setTargetValue(m_longSo.targetValue);
    m_deviceController.addBarrierArm(arbitratorStream, false, requiredCredits, {currentStream.getStreamIndex()});
    m_deviceController.waitForBarrierArm(currentStream);

    HclCollectiveParams collectiveParams {eHCLNoCollective,
                                          nullptr,
                                          0x0,
                                          0x0,
                                          0x0,
                                          hcclFloat32,
                                          m_device->getComm(comm),
                                          0,
                                          HCL_DEFAULT_API_ID,
                                          hcclOpNone,
                                          0};

    CommonState commonState {
        collectiveParams,
        m_intermediateBufferManager,
        m_scaleoutProvider->isHostNic(),
        m_scaleoutProvider->isGaudiDirect(),
        m_device->getEdmaEngineWorkDistributionSize(),
        m_device->getHal()->getMaxNumScaleUpPortsPerConnection(),
        (m_device->getPortMapping()).getNumScaleOutPorts(collectiveParams.m_dynamicComm.getSpotlightType()),
        m_device->getDeviceType(),
        this->m_remainderCalculator};

    // count scaleup signals
    unsigned numSignals = countScaleUpSignalsSendRecv(commonState,
                                                      numberOfSendBuckets,
                                                      numberOfRecvBuckets,
                                                      numberOfSends,
                                                      numberOfRecvs);

    const unsigned isForceOrder = m_graphSync.isForceOrder(true);

    numSignals += isForceOrder + (memcopyVec.size() * commonState.signalToCost(SignalEvent::EDMA_MEMCOPY))+ scaleoutSignals;
    LOG_HCL_TRACE(HCL, "isForceOrder={}, numSignals={}", isForceOrder, numSignals);

    m_commands.serializeLbwWriteCommand(currentStream,
                                        currentStream.getSchedIdx(),
                                        m_graphSync.getCurrentCgSoAddr(CgType::eExternal),
                                        m_graphSync.getSoConfigValue(COMP_SYNC_GROUP_CMAX_TARGET - numSignals, true));

    for (const SendRecvMemCopyEntry& var : memcopyVec)
    {
        m_memHandler->createMemCopyCommandsNonCollective(currentStream,
                                                         m_device->getMyRank(comm),
                                                         var.chunkCount,
                                                         var.dataType,
                                                         var.recvBaseAddress,
                                                         var.sendBaseAddress,
                                                         commonState.m_dynamicComm.getScaleupGroupSize(),
                                                         apiId);
    }

    if (numberOfSendBuckets > 0)
    {
        uint32_t sendIndex = 0;
        this->createScaleUpSendRecvOp(currentStream,
                                      sendVec,
                                      m_device->getComm(comm).m_rankInfo.header.hwModuleID,
                                      comm,
                                      0,
                                      m_graphSync.getCurrentCgSoAddr(CgType::eExternal),
                                      true,
                                      ++sendIndex == numberOfSendBuckets,
                                      false,
                                      false);
    }
}

void HclCollectiveRoutinesGen2Arch::createScaleUpRecvProgsNonCollective(uint32_t             numberOfRecv,
                                                                        const SendRecvArray& recvVec,
                                                                        HCL_Comm             comm,
                                                                        unsigned             requiredCredits)
{
    LOG_HCL_TRACE(HCL, "numberOfRecv={}, requiredCredits={}, recvVec={}", numberOfRecv, requiredCredits, recvVec);

    hcl::ScalStream& currentStream =
        ActiveStreamManagerGen2Arch::getActiveCollectiveStream(m_deviceController,
                                                               eHCLNoCollective,
                                                               m_streamId,
                                                               (unsigned)hcl::SchedulersIndex::recvScaleUp);
    currentStream.setTargetValue(m_longSo.targetValue);

    hcl::ScalStream& arbitratorStream =
        m_deviceController.getScalStream(m_streamId, currentStream.getSchedIdx(), ARB_STREAM_IDX);
    arbitratorStream.setTargetValue(m_longSo.targetValue);
    m_deviceController.addBarrierArm(arbitratorStream, false, requiredCredits, {currentStream.getStreamIndex()});
    m_deviceController.waitForBarrierArm(currentStream);

    if (numberOfRecv > 0)
    {
        uint32_t recvIndex = 0;
        auto     wraparoundBits = m_wqeTracker->getWqeWraparoundBits(
            comm,
            0,
            currentStream.getStreamIndex() == 0 ? QpType::ScaleUpReduceScatter : QpType::ScaleUpAllGather);
        this->createScaleUpSendRecvOp(currentStream,
                                      recvVec,
                                      m_device->getComm(comm).m_rankInfo.header.hwModuleID,
                                      comm,
                                      0,
                                      m_graphSync.getCurrentCgSoAddr(CgType::eExternal),
                                      false,
                                      ++recvIndex == numberOfRecv,
                                      wraparoundBits.notify_rndv_ack,
                                      wraparoundBits.wait_for_rndv_acks);
    }
}

void HclCollectiveRoutinesGen2Arch::createDmaProgsNonCollective(unsigned int sizeInBytes, unsigned requiredCredits)
{
    LOG_HCL_TRACE(HCL, "sizeInBytes={}, requiredCredits={}", sizeInBytes, requiredCredits);
    const unsigned     dmaSchedIdx            = (unsigned)hcl::SchedulersIndex::dma;
    hcl::ScalStream&   arbStream =
        m_deviceController.getScalStream(m_streamId, dmaSchedIdx, static_cast<unsigned>(hcl::DMAStreams::arbitrator));
    constexpr unsigned streamIdx              = 0;
    hcl::ScalStream&   garbageCollectorStream = m_deviceController.getScalStream(m_streamId, dmaSchedIdx, streamIdx);
    arbStream.setTargetValue(m_longSo.targetValue);
    garbageCollectorStream.setTargetValue(m_longSo.targetValue);

    m_deviceController.addBarrierArm(arbStream, false, requiredCredits, {streamIdx});

    m_deviceController.waitForBarrierArm(garbageCollectorStream);
    m_deviceController.addBarrierArm(garbageCollectorStream, true, 1 /*creditsNr*/, {});

    m_graphSync.createResetSoMessages(garbageCollectorStream,
                                      dmaSchedIdx,
                                      m_deviceController.getSyncParams(m_streamId).m_smInfo.soSmIndex,
                                      m_signalsManager->getMethodsToClean());

    const unsigned additionalSignal = m_graphSync.isForceOrder(false) ? 1 : 0;

    LOG_HCL_TRACE(HCL,
                  "Count-Signaling | Internal cg is set to wait on 0x{:x}, signals: {}",
                  uint64_t(COMP_SYNC_GROUP_CMAX_TARGET - additionalSignal),
                  additionalSignal);
    m_commands.serializeLbwWriteCommand(
        garbageCollectorStream,
        dmaSchedIdx,
        m_graphSync.getCurrentCgSoAddr(CgType::eInternal),
        m_graphSync.getSoConfigValue(COMP_SYNC_GROUP_CMAX_TARGET - additionalSignal, true));
    m_deviceController.incInternalCgTargetValue(m_streamId);
}

void HclCollectiveRoutinesGen2Arch::createScaleUpSendProgs(SliceState&      sendSliceState,
                                                           unsigned         sliceIter,
                                                           BoxNumInfo&      boxNumInfo,
                                                           unsigned         requiredCredits,
                                                           HCL_CollectiveOp currentOp,
                                                           unsigned         numSignals)
{
    hcl::ScalStream& currentStream =
        ActiveStreamManagerGen2Arch::getActiveCollectiveStream(m_deviceController,
                                                               currentOp,
                                                               m_streamId,
                                                               (unsigned)hcl::SchedulersIndex::sendScaleUp);
    bool isPeersOnly = sendSliceState.m_isMultiScaleupGroup && sendSliceState.m_dynamicComm.getScaleupGroupSize() == 1;

    currentStream.setTargetValue(m_longSo.targetValue);

    m_deviceController.waitForBarrierArm(currentStream);

    hcl::ScalStream& arbitratorStream =
        m_deviceController.getScalStream(m_streamId, currentStream.getSchedIdx(), ARB_STREAM_IDX);
    arbitratorStream.setTargetValue(m_longSo.targetValue);

    m_deviceController.addBarrierArm(arbitratorStream, false, requiredCredits, {currentStream.getStreamIndex()});

    LOG_HCL_CONTEXT_TRACE(HCL, "Serializing scaleup send scheduler commands");

    if (sendSliceState.gatherOpsWaitForRS(true))
    {
        streamAddSingleWaitIfNeeded(currentStream, {WaitEvent::RR_GATHER_OPS_WAIT_FOR_RS});
    }

    m_commands.serializeLbwWriteCommand(currentStream,
                                        currentStream.getSchedIdx(),
                                        m_signalsManager->getSoAddress(WaitMethod::EXTERNAL_CG_SO),
                                        m_graphSync.getSoConfigValue(COMP_SYNC_GROUP_CMAX_TARGET - numSignals, true));

    if (m_signalsManager->isEventRegistered(SignalEvent::EDMA_MEMCOPY) ||
        m_signalsManager->isEventRegistered(SignalEvent::EDMA_CAST_UP))
    {
        uint64_t chunkCount = sendSliceState.getChunkCount();

        if (currentOp == eHCLReduceScatter || sendSliceState.m_collectiveOp == eHCLBroadcast)
        {
            chunkCount = sendSliceState.m_rankScaleOutCount;
        }
        else if (sendSliceState.m_collectiveOp == eHCLSinglePeerBroadcast && sendSliceState.isRoot())
        {
            chunkCount = sendSliceState.m_boxCount;
        }

        syncWithLtuIfNeeded(sendSliceState, currentStream);

        // first copy for not-in-place
        m_memHandler->createMemCopyCommands(
            sendSliceState,
            m_signalsManager,
            sliceIter,
            boxNumInfo,
            chunkCount,
            currentStream,
            (sendSliceState.m_16BitReduction && (!sendSliceState.m_isReductionCollective || isPeersOnly))
                ? m_commands.getDmaTypeCastUp()
                : m_commands.getDmaTypeMemCpy(),
            false,
            0,
            false,
            false,
            false,
            NO_POOL /* DONT CARE - poolId*/);
    }

    if (m_signalsManager->isEventRegistered(SignalEvent::SCALEUP_SEND))
    {
        box_devices_t deviceToRemoteIndex;
        getDeviceToRemoteIndex(sendSliceState, true, deviceToRemoteIndex);

        streamAddSingleWaitIfNeeded(currentStream,
                                    {WaitEvent::SO_FIRST_SU_SEND_WAIT_FOR_SO_RECV,
                                     WaitEvent::COMPLEX_BCAST_AG_SU_SEND_WAIT_FOR_SCATTER_RECV,
                                     WaitEvent::COMPLEX_BCAST_SO_SEND_WAIT_FOR_SO_RECV,
                                     WaitEvent::COMPLEX_BCAST_SO_SEND_AND_AG_SU_WAIT_FOR_SU_RECV,
                                     WaitEvent::COMPLEX_BCAST_SO_SEND_AND_AG_SU_WAIT_FOR_SO_RECV});

        uint64_t count = sendSliceState.m_boxCount;
        uint64_t cellCount = sendSliceState.getChunkCount();
        uint64_t strideCount = sendSliceState.getStrideCount();
        uint64_t offset =
            sendSliceState.m_dynamicComm.getRankInScaleupGroup() * strideCount * sendSliceState.m_dataTypeSizeInBytes;
        uint64_t baseAddress =
            m_addressGenerator->generateScaleUpSendAddress(sendSliceState, sliceIter, boxNumInfo, currentOp, offset);

        LOG_TRACE(HCL_ECR,
                  "Counts for Scaleup Send: op {}, box {}, slice {}, cell count {}, stride {}, count {}, "
                  "has buffer {}, address 0x{:X}",
                  currentOp,
                  boxNumInfo.m_boxNum,
                  sliceIter,
                  cellCount,
                  strideCount,
                  count,
                  sendSliceState.m_hasBufferSize,
                  baseAddress);

        ScaleUpCollectiveOp op {deviceToRemoteIndex,
                                sendSliceState.m_dynamicComm.m_rankInfo.header.hwModuleID,
                                sendSliceState.m_dynamicComm,
                                currentOp,
                                sendSliceState.m_reduceOp,
                                m_streamId * 2 + currentStream.getStreamIndex(),
                                m_signalsManager->dequeueSoAddress(SignalEvent::SCALEUP_SEND),
                                true /*send*/,
                                sendSliceState.isComplexImplementation(),
                                sendSliceState.m_isReductionCollective,
                                sendSliceState.m_isMultiScaleupGroup,
                                baseAddress,
                                count,
                                sendSliceState.m_hasBufferSize && sendSliceState.isLastSlice(sliceIter),
                                sendSliceState.m_dataType,
                                cellCount,
                                strideCount,
                                false,
                                false,
                                sendSliceState.m_isReductionCollective,
                                0,
                                0,
                                sendSliceState.m_collectiveOp,
                                sendSliceState.isRoot()};

        this->createScaleUpCollectiveOp(currentStream, op);
    }
}

void HclCollectiveRoutinesGen2Arch::calculateScaleupSignals(CommonState& commonState,
                                                            BoxNumInfo&  boxNumInfo,
                                                            bool         isLastBox,
                                                            bool         isFirstBox)
{
    bool isPeersOnly         = commonState.m_isMultiScaleupGroup && commonState.m_dynamicComm.getScaleupGroupSize() == 1;

    if (m_graphSync.isForceOrder(true))
    {
        m_signalsManager->enqueueCompletion({SignalEvent::FORCE_ORDER});
    }

    switch (commonState.m_currentOp)
    {
        case eHCLScatter:
        {
            if (commonState.m_collectiveOp == eHCLBroadcast)
            {
                // We Scatter only on the root box
                if (isFirstBox && boxNumInfo.m_boxNum == commonState.rootBox())
                {
                    if (!commonState.isRoot())
                    {
                        // ScaleOut send and AG ScaleUp wait for scaleup recieve
                        unsigned int numFences = commonState.m_isMultiScaleupGroup ? 2 : 1;
                        m_signalsManager->enqueueWait(WaitEvent::COMPLEX_BCAST_SO_SEND_AND_AG_SU_WAIT_FOR_SU_RECV,
                                                      {SignalEvent::SCALEUP_RECV},
                                                      WaitMethod::GPSO_LONGTERM,
                                                      0,
                                                      numFences);
                    }
                    else
                    {
                        m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND});
                        if (!commonState.m_inPlace)
                        {
                            m_signalsManager->enqueueCompletion({SignalEvent::EDMA_MEMCOPY});
                        }
                    }
                }
                break;
            }
            else  // eHCLSinglePeerBroadcast
            {
                if (isPeersOnly)
                {
                    if (isFirstBox && boxNumInfo.m_boxNum == commonState.rootBox() && commonState.isRootOrRootPeer() &&
                        !commonState.m_inPlace)
                    {
                        m_signalsManager->enqueueCompletion({SignalEvent::EDMA_MEMCOPY});
                    }
                }
                else if (isFirstBox && boxNumInfo.m_boxNum == commonState.rootBox())
                {
                    if (!commonState.isRootOrRootPeer())
                    {
                        m_signalsManager->enqueueWait(WaitEvent::COMPLEX_BCAST_AG_SU_SEND_WAIT_FOR_SCATTER_RECV,
                                                      {SignalEvent::SCALEUP_RECV},
                                                      WaitMethod::GPSO_LONGTERM);
                    }
                    else
                    {
                        m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND});
                        if (!commonState.m_inPlace)
                        {
                            m_signalsManager->enqueueCompletion({SignalEvent::EDMA_MEMCOPY});
                        }
                    }
                }
                else if (boxNumInfo.m_boxNum ==
                             getPrevBox(commonState.m_dynamicComm.getMyScaleupGroup(), commonState.m_boxIterations) &&
                         commonState.m_dynamicComm.getMyScaleupGroup() != commonState.rootBox())
                {
                    if (commonState.isRootOrRootPeer())
                    {
                        m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND});
                    }
                    else
                    {
                        m_signalsManager->enqueueWait(WaitEvent::COMPLEX_BCAST_AG_SU_SEND_WAIT_FOR_SCATTER_RECV,
                                                      {SignalEvent::SCALEUP_RECV},
                                                      WaitMethod::GPSO_LONGTERM);
                    }
                }
                break;
            }
        }

        case eHCLSimpleBroadcast:
        {
            if (isPeersOnly)
            {
                if (isFirstBox && boxNumInfo.m_boxNum == commonState.rootBox() && commonState.isRootOrRootPeer() &&
                    !commonState.m_inPlace)
                {
                    m_signalsManager->enqueueCompletion({SignalEvent::EDMA_MEMCOPY});
                }
            }
            else if (commonState.isRoot())
            {
                if (!commonState.m_isMultiScaleupGroup || isFirstBox)
                {
                    m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND});
                    if (!commonState.m_inPlace)
                    {
                        m_signalsManager->enqueueCompletion({SignalEvent::EDMA_MEMCOPY});
                    }
                }
            }
            else if (boxNumInfo.m_boxNum == commonState.rootBox())
            {
                if (commonState.isRootPeer())
                {
                    // scaleout recv -> scaleup send
                    m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND});
                }
                else
                {
                    // scaleup recv from root peer
                    m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_RECV});
                }
            }
            break;
        }

        case eHCLAllGather:
        {
            if (commonState.m_collectiveOp == eHCLBroadcast)
            {
                if (isFirstBox)
                {
                    m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND, SignalEvent::SCALEUP_RECV});
                }
            }
            else if (commonState.m_collectiveOp == eHCLSinglePeerBroadcast)
            {
                if (isFirstBox && boxNumInfo.m_boxNum == commonState.rootBox())
                {
                    if (!commonState.isRootOrRootPeer())
                    {
                        m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND, SignalEvent::SCALEUP_RECV});
                    }
                }
                else if (boxNumInfo.m_boxNum ==
                             getPrevBox(commonState.m_dynamicComm.getMyScaleupGroup(), commonState.m_boxIterations) &&
                         commonState.m_dynamicComm.getMyScaleupGroup() != commonState.rootBox())
                {
                    if (!commonState.isRootOrRootPeer())
                    {
                        m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND, SignalEvent::SCALEUP_RECV});
                    }
                }
            }
            else if (isPeersOnly)
            {
                if (isFirstBox && !commonState.m_inPlace && commonState.m_collectiveOp != eHCLAllReduce)
                {
                    m_signalsManager->enqueueCompletion({SignalEvent::EDMA_MEMCOPY});
                }
            }
            else
            {
                m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND, SignalEvent::SCALEUP_RECV});
                if (isFirstBox && !commonState.m_inPlace && commonState.m_collectiveOp != eHCLAllReduce)
                {
                    m_signalsManager->enqueueCompletion({SignalEvent::EDMA_MEMCOPY});
                }
            }

            break;
        }

        case eHCLGather:
        {
            // only root box scale up - on every box iteration (after scale out)
            if (commonState.m_dynamicComm.getMyScaleupGroup() == commonState.rootBox() && !isPeersOnly)
            {
                if (commonState.isRoot())
                {
                    m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_RECV});
                }
                else // non root - send to root
                {
                    m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND});
                }
            }
            break;
        }

        case eHCLReduceScatter:
        {
            if (commonState.m_collectiveOp == eHCLReduce || commonState.m_collectiveOp == eHCLAllReduce)
            {
                return calculateScaleupSignalsReduceScatterForOtherOps(commonState, isLastBox, isFirstBox);
            }
            else
            {
                return calculateScaleupSignalsReduceScatter(commonState, isLastBox, isFirstBox);
            }
        }

        case eHCLAll2All:
        {
            if (isPeersOnly)
            {
                if (boxNumInfo.m_boxNum == commonState.m_dynamicComm.getMyScaleupGroup())
                {
                    m_signalsManager->enqueueCompletion({SignalEvent::EDMA_MEMCOPY});
                }
            }
            else if (commonState.m_all2allIter == 0)
            {
                m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND});

                if (!commonState.m_isMultiScaleupGroup || boxNumInfo.m_boxNum == commonState.m_dynamicComm.getMyScaleupGroup())
                {
                    m_signalsManager->enqueueCompletion({SignalEvent::EDMA_MEMCOPY, SignalEvent::SCALEUP_RECV});
                }
                else
                {
                    if (commonState.m_all2allIterations > 1)
                    {
                        m_signalsManager->enqueueWait(WaitEvent::ALL2ALL_SO_SEND_WAIT_FOR_RECV,
                                                      {SignalEvent::EDMA_MEMCOPY, SignalEvent::SCALEUP_RECV},
                                                      WaitMethod::GPSO_LONGTERM,
                                                      0,
                                                      commonState.m_all2allIterations);
                    }
                    else
                    {
                        m_signalsManager->enqueueWait(WaitEvent::ALL2ALL_SO_SEND_WAIT_FOR_RECV,
                                                      {SignalEvent::EDMA_MEMCOPY, SignalEvent::SCALEUP_RECV},
                                                      WaitMethod::GPSO_0);
                    }
                }
            }
            break;
        }
        case eHCLAllReduce:
        case eHCLReduce:
        case eHCLBroadcast:
        case eHCLSinglePeerBroadcast:
        case eHCLNoCollective:
        case eHCLCollectiveLastValue:
            VERIFY(false, "Attempting to calculate scaleup resources for currentOp = {}", commonState.m_currentOp);
    }
}

void HclCollectiveRoutinesGen2Arch::calculateScaleupSignalsReduceScatter(CommonState& commonState,
                                                                         bool         isLastBox,
                                                                         bool         isFirstBox)
{
    bool isHierarchicalFirst = commonState.m_isMultiScaleupGroup && isFirstBox;
    bool isPeersOnly         = commonState.m_isMultiScaleupGroup &&
                               commonState.m_dynamicComm.getScaleupGroupSize() == 1;

    BoxNumInfo myBoxNumInfo(commonState.m_dynamicComm.getMyScaleupGroup(), BoxNumInfo::boxOrientation::MY_BOX);

    if (isPeersOnly)
    {
        if (isHierarchicalFirst)
        {
            bool     isEdgeIteration = commonState.isEdgeIteration(myBoxNumInfo);
            unsigned longtermOffset  = isEdgeIteration ? commonState.m_reproScaleoutLongtermAmount - 1 : 0;

            WaitEvent waitEventForSoRecv =
                isEdgeIteration
                    ? WaitEvent::RR_RS_SO_WAIT_FOR_ALL_RECV
                    : (WaitEvent)((unsigned)WaitEvent::RR_RS_SO_RECV_WAIT_FOR_PREV_RECV_BASE + longtermOffset);

            m_signalsManager->enqueueWait(
                waitEventForSoRecv,
                {commonState.m_16BitReduction ? SignalEvent::EDMA_CAST_UP : SignalEvent::EDMA_MEMCOPY},
                WaitMethod::GPSO_LONGTERM,
                0,
                1,
                longtermOffset);
        }
    }
    else if (commonState.m_isMultiScaleupGroup && isLastBox)
    {
        m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND});
    }
    else if (!commonState.m_isMultiScaleupGroup)
    {
        m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND});
        m_signalsManager->enqueueCompletion({SignalEvent::EDMA_BATCH});
    }
    else if (isFirstBox)
    {
        bool     isEdgeIteration = commonState.isEdgeIteration(myBoxNumInfo);
        unsigned longtermOffset  = isEdgeIteration ? commonState.m_reproScaleoutLongtermAmount - 1 : 0;

        WaitEvent waitEventForSoRecv =
            isEdgeIteration ? WaitEvent::RR_RS_SO_WAIT_FOR_ALL_RECV
                            : (WaitEvent)((unsigned)WaitEvent::RR_RS_SO_RECV_WAIT_FOR_PREV_RECV_BASE + longtermOffset);

        m_signalsManager->enqueueWait(waitEventForSoRecv,
                                      {SignalEvent::RR_SIGNAL_TO_LONGTERM},
                                      WaitMethod::GPSO_LONGTERM,
                                      0,
                                      1,
                                      longtermOffset);
        m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND, SignalEvent::EDMA_BATCH});
    }
    else
    {
        m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND});
    }

    if (!isPeersOnly)
    {
        WaitMethod waitMethodForRR = WaitMethod::GPSO_0;
        WaitEvent  waitEventForEdmaBatch;

        if (commonState.m_isMultiScaleupGroup && !isFirstBox)
        {
            waitEventForEdmaBatch = WaitEvent::RR_SCALEOUT_SEND_WAIT_FOR_DMA;
        }
        else
        {
            waitEventForEdmaBatch = WaitEvent::RR_FINAL_DMA_WAIT_FOR_EDMA;
        }

        // First chain
        m_signalsManager->enqueueWait(WaitEvent::RR_DMA_WAIT_FOR_SU_RECV,
                                      {SignalEvent::SCALEUP_RECV},
                                      waitMethodForRR,
                                      0);

        if (commonState.m_isMultiScaleupGroup && !isFirstBox)
        {
            m_signalsManager->enqueueWait(waitEventForEdmaBatch, {SignalEvent::EDMA_BATCH}, waitMethodForRR, 1);
        }

        if (commonState.m_syncUpBufferWithLtu && !isFirstBox)
        {
            m_signalsManager->enqueueCompletion({SignalEvent::RR_SIGNAL_TO_CG});
        }
    }

    if (commonState.m_isMultiScaleupGroup && isLastBox)
    {
        if (m_scaleoutProvider->isGaudiDirect())
        {
            m_signalsManager->enqueueWait(WaitEvent::HNIC_SIGNAL_SPLIT_WAIT_FOR_GDR_MEMCPY,
                                          {SignalEvent::EDMA_MEMCOPY_GDR},
                                          WaitMethod::GPSO_1,
                                          1);
        }
        m_signalsManager->enqueueCompletion({SignalEvent::EDMA_BATCH_SCALEOUT});
    }
    else if (m_scaleoutProvider->isGaudiDirect() && !isFirstBox)
    {
        m_signalsManager->enqueueCompletion({SignalEvent::EDMA_MEMCOPY_GDR});
    }
}

void HclCollectiveRoutinesGen2Arch::calculateScaleupSignalsReduceScatterForOtherOps(CommonState& commonState,
                                                                                    bool         isLastBox,
                                                                                    bool         isFirstBox)
{
    bool isMultiScaleupGroup = commonState.m_isMultiScaleupGroup;
    bool isHierarchicalFirst = isMultiScaleupGroup && isFirstBox;
    bool isHierarchicalLast  = isMultiScaleupGroup && isLastBox;
    bool isPeersOnly         = isMultiScaleupGroup && commonState.m_dynamicComm.getScaleupGroupSize() == 1;
    bool isReduceRoot        = commonState.m_collectiveOp == eHCLReduce && commonState.m_isRoot;
    bool gatherOpsWaitForRS  = !(isReduceRoot);
    BoxNumInfo myBoxNumInfo(commonState.m_dynamicComm.getMyScaleupGroup(), BoxNumInfo::boxOrientation::MY_BOX);

    if (isPeersOnly)
    {
        if (isHierarchicalFirst)
        {
            bool     isEdgeIteration = commonState.isEdgeIteration(myBoxNumInfo);
            unsigned longtermOffset  = isEdgeIteration ? commonState.m_reproScaleoutLongtermAmount - 1 : 0;

            WaitEvent waitEventForSoRecv =
                isEdgeIteration
                    ? WaitEvent::RR_RS_SO_WAIT_FOR_ALL_RECV
                    : (WaitEvent)((unsigned)WaitEvent::RR_RS_SO_RECV_WAIT_FOR_PREV_RECV_BASE + longtermOffset);

            m_signalsManager->enqueueWait(
                waitEventForSoRecv,
                {commonState.m_16BitReduction ? SignalEvent::EDMA_CAST_UP : SignalEvent::EDMA_MEMCOPY},
                WaitMethod::GPSO_LONGTERM,
                0,
                1,
                longtermOffset);
        }
        else if (isHierarchicalLast)
        {
            if (gatherOpsWaitForRS)
            {
                int numExpectedFences = commonState.m_collectiveOp == eHCLReduce ? 1 : 2;
                m_signalsManager->enqueueWait(WaitEvent::RR_GATHER_OPS_WAIT_FOR_RS,
                                              {SignalEvent::EDMA_BATCH_SCALEOUT},
                                              WaitMethod::GPSO_LONGTERM,
                                              1,
                                              numExpectedFences,
                                              commonState.m_reproScaleoutLongtermAmount - 1);
            }
            else  // Reduce root
            {
                m_signalsManager->enqueueCompletion({SignalEvent::EDMA_BATCH_SCALEOUT});
            }
        }
    }
    else if (commonState.m_isMultiScaleupGroup && isLastBox)
    {
        m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND});
        if (gatherOpsWaitForRS)
        {
            int numExpectedFences = commonState.m_collectiveOp == eHCLReduce ? 1 : 2;
            m_signalsManager->enqueueWait(WaitEvent::RR_GATHER_OPS_WAIT_FOR_RS,
                                          {SignalEvent::EDMA_BATCH_SCALEOUT},
                                          WaitMethod::GPSO_LONGTERM,
                                          1,
                                          numExpectedFences,
                                          commonState.m_reproScaleoutLongtermAmount - 1);
        }
        else  // Reduce root
        {
            m_signalsManager->enqueueCompletion({SignalEvent::EDMA_BATCH_SCALEOUT});
        }
    }
    else if (!commonState.m_isMultiScaleupGroup)
    {
        m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND});

        if (gatherOpsWaitForRS)
        {
            m_signalsManager->enqueueWait(WaitEvent::RR_GATHER_OPS_WAIT_FOR_RS,
                                          {SignalEvent::EDMA_BATCH},
                                          WaitMethod::GPSO_LONGTERM,
                                          1);
        }
        else  // Reduce root
        {
            m_signalsManager->enqueueCompletion({SignalEvent::EDMA_BATCH});
        }
    }
    else if (isFirstBox)
    {

        bool     isEdgeIteration = commonState.isEdgeIteration(myBoxNumInfo);
        unsigned longtermOffset  = isEdgeIteration ? commonState.m_reproScaleoutLongtermAmount - 1 : 0;

        WaitEvent waitEventForSoRecv =
            isEdgeIteration ? WaitEvent::RR_RS_SO_WAIT_FOR_ALL_RECV
                            : (WaitEvent)((unsigned)WaitEvent::RR_RS_SO_RECV_WAIT_FOR_PREV_RECV_BASE + longtermOffset);

        m_signalsManager->enqueueWait(waitEventForSoRecv,
                                      {SignalEvent::RR_SIGNAL_TO_LONGTERM},
                                      WaitMethod::GPSO_LONGTERM,
                                      0,
                                      1,
                                      longtermOffset);

        m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND, SignalEvent::EDMA_BATCH});
    }
    else
    {
        m_signalsManager->enqueueCompletion({SignalEvent::SCALEUP_SEND});
    }

    if (!isPeersOnly)
    {
        WaitMethod waitMethodForRR =
            (!commonState.m_isMultiScaleupGroup && !isReduceRoot) ? WaitMethod::GPSO_LONGTERM : WaitMethod::GPSO_0;

        m_signalsManager->enqueueWait(WaitEvent::RR_DMA_WAIT_FOR_SU_RECV,
                                      {SignalEvent::SCALEUP_RECV},
                                      waitMethodForRR,
                                      0);

        if (commonState.m_isMultiScaleupGroup && !isFirstBox)
        {
            WaitEvent waitEventForEdmaBatch;
            if (commonState.m_isMultiScaleupGroup && !isFirstBox)
            {
                waitEventForEdmaBatch = WaitEvent::RR_SCALEOUT_SEND_WAIT_FOR_DMA;
            }
            else
            {
                waitEventForEdmaBatch = WaitEvent::RR_FINAL_DMA_WAIT_FOR_EDMA;
            }

            m_signalsManager->enqueueWait(waitEventForEdmaBatch, {SignalEvent::EDMA_BATCH}, waitMethodForRR, 1);
        }

        if (commonState.m_syncUpBufferWithLtu && !isFirstBox)
        {
            m_signalsManager->enqueueCompletion({SignalEvent::RR_SIGNAL_TO_CG});
        }
    }

    if (commonState.m_isMultiScaleupGroup && isLastBox)
    {
        if (m_scaleoutProvider->isGaudiDirect())
        {
            m_signalsManager->enqueueWait(WaitEvent::HNIC_SIGNAL_SPLIT_WAIT_FOR_GDR_MEMCPY,
                                            {SignalEvent::EDMA_MEMCOPY_GDR},
                                            WaitMethod::GPSO_1,
                                            1);
        }
    }
    else if (m_scaleoutProvider->isGaudiDirect() && !isFirstBox)
    {
        m_signalsManager->enqueueCompletion({SignalEvent::EDMA_MEMCOPY_GDR});
    }
}

void HclCollectiveRoutinesGen2Arch::advanceProg(bool nopOp, uint64_t cuid, CommonState* commonState)
{
    if (commonState != nullptr)
    {
        m_signalsManager->initialize(commonState, cuid);
    }

    m_deviceController.advanceProg(m_streamId, nopOp);
}

void HclCollectiveRoutinesGen2Arch::createScaleUpRecvProgs(SliceState&      sliceState,
                                                           unsigned         sliceIter,
                                                           BoxNumInfo&      boxNumInfo,
                                                           unsigned         requiredCredits,
                                                           HCL_CollectiveOp currentOp)
{
    hcl::ScalStream& currentStream =
        ActiveStreamManagerGen2Arch::getActiveCollectiveStream(m_deviceController,
                                                               currentOp,
                                                               m_streamId,
                                                               (unsigned)hcl::SchedulersIndex::recvScaleUp);
    currentStream.setTargetValue(m_longSo.targetValue);

    m_deviceController.waitForBarrierArm(currentStream);

    hcl::ScalStream& arbitratorStream =
        m_deviceController.getScalStream(m_streamId, currentStream.getSchedIdx(), ARB_STREAM_IDX);
    arbitratorStream.setTargetValue(m_longSo.targetValue);

    m_deviceController.addBarrierArm(arbitratorStream, false, requiredCredits, {currentStream.getStreamIndex()});

    LOG_HCL_CONTEXT_TRACE(HCL, "Serializing scaleup recv scheduler command");

    if (m_signalsManager->isEventRegistered(SignalEvent::SCALEUP_RECV))
    {
        box_devices_t deviceToRemoteIndex;
        getDeviceToRemoteIndex(sliceState, false, deviceToRemoteIndex, currentStream.getStreamIndex());

        hcclDataType_t dataType = (currentOp != eHCLAllGather && currentOp != eHCLGather &&
                                   sliceState.m_16BitReduction && !sliceState.m_isReductionCollective)
                                      ? hcclFloat32
                                      : sliceState.m_dataType;

        uint64_t count       = sliceState.m_boxCount;
        uint64_t strideCount = (sliceState.m_collectiveOp == eHCLAll2All && sliceState.m_isSlicing &&
                                boxNumInfo.m_boxNum != sliceState.m_dynamicComm.getMyScaleupGroup())
                                   ? sliceState.getChunkCount()
                                   : sliceState.getStrideCount();
        uint64_t offset      = sliceState.m_dynamicComm.getRankInScaleupGroup() * strideCount *
                               sliceState.m_dataTypeSizeInBytes;

        uint64_t baseAddress = 0;
        uint32_t accuIndex   = 0;
        uint32_t rrIndex     = 0;

        m_memHandler
            ->generateBaseAddressOrRRIdx(sliceState, sliceIter, boxNumInfo, currentOp, offset, baseAddress, rrIndex);

        auto wraparoundBits = m_wqeTracker->getWqeWraparoundBits(
            sliceState.m_dynamicComm,
            0,
            currentStream.getStreamIndex() == 0 ? QpType::ScaleUpReduceScatter : QpType::ScaleUpAllGather);

        LOG_TRACE(HCL_ECR,
                  "Counts for Scaleup Recv: op {}, box {}, slice {}, cell count {}, stride {}, count {}, "
                  "has buffer {}, address 0x{:X}",
                  currentOp,
                  boxNumInfo.m_boxNum,
                  sliceIter,
                  sliceState.getChunkCount(),
                  strideCount,
                  count,
                  sliceState.m_hasBufferSize,
                  baseAddress);

        syncWithLtuIfNeeded(sliceState, currentStream);

        ScaleUpCollectiveOp op {
            deviceToRemoteIndex,
            sliceState.m_dynamicComm.m_rankInfo.header.hwModuleID,
            sliceState.m_dynamicComm,
            currentOp,
            sliceState.m_reduceOp,
            m_streamId * 2 + currentStream.getStreamIndex(),
            m_signalsManager->dequeueSoAddress(SignalEvent::SCALEUP_RECV),
            false /*receive*/,
            sliceState.isComplexImplementation(),
            sliceState.m_isReductionCollective,
            sliceState.m_isMultiScaleupGroup && !(sliceState.m_collectiveOp == eHCLReduce &&
                                         boxNumInfo.m_boxNum == sliceState.m_dynamicComm.getMyScaleupGroup()),
            baseAddress,
            count,
            sliceState.m_hasBufferSize && sliceState.isLastSlice(sliceIter),
            dataType,
            sliceState.getChunkCount(),
            strideCount,
            wraparoundBits.notify_rndv_ack,
            wraparoundBits.wait_for_rndv_acks,
            sliceState.m_isReductionCollective && (currentOp != eHCLAllGather && currentOp != eHCLGather),
            accuIndex,
            rrIndex,
            sliceState.m_collectiveOp,
            sliceState.isRoot()};

        this->createScaleUpCollectiveOp(currentStream, op);
    }
}

void HclCollectiveRoutinesGen2Arch::createScaleOutSendProgs(SliceState& sliceState,
                                                            unsigned    requiredCredits)
{
    hcl::ScalStream& currentStream =
        ActiveStreamManagerGen2Arch::getActiveCollectiveStream(m_deviceController,
                                                               sliceState.m_currentOp,
                                                               m_streamId,
                                                               (unsigned)hcl::SchedulersIndex::sendScaleOut);
    currentStream.setTargetValue(m_longSo.targetValue);

    hcl::ScalStream& arbitratorStream =
        m_deviceController.getScalStream(m_streamId, currentStream.getSchedIdx(), ARB_STREAM_IDX);
    arbitratorStream.setTargetValue(m_longSo.targetValue);

    BarrierArbitratorDescriptor desc {*this,
                                      *m_scaleoutProvider,
                                      currentStream,
                                      arbitratorStream,
                                      m_streamId,
                                      currentStream.getStreamIndex(),
                                      currentStream.getSchedIdx(),
                                      requiredCredits,
                                      m_longSo};
    desc.run(sliceState);

    LOG_HCL_CONTEXT_TRACE(HCL, "Serializing scaleout send scheduler commands");

    if (sliceState.gatherOpsWaitForRS(false))
    {
        streamAddSingleWaitIfNeeded(currentStream, {WaitEvent::RR_GATHER_OPS_WAIT_FOR_RS});
    }

    if (m_signalsManager->isEventRegistered(SignalEvent::SCALEOUT_SEND) ||
        m_signalsManager->isEventRegistered(SignalEvent::HNIC_SCALEOUT_SEND))
    {
        streamAddSingleWaitIfNeeded(currentStream,
                                    {WaitEvent::ALL2ALL_SO_SEND_WAIT_FOR_RECV,
                                     WaitEvent::COMPLEX_BCAST_SO_SEND_WAIT_FOR_SO_RECV,
                                     WaitEvent::COMPLEX_BCAST_SO_SEND_AND_AG_SU_WAIT_FOR_SU_RECV,
                                     WaitEvent::COMPLEX_BCAST_SO_SEND_AND_AG_SU_WAIT_FOR_SO_RECV,
                                     WaitEvent::RR_SCALEOUT_SEND_WAIT_FOR_DMA});

        if (!m_scaleoutProvider->isHostNic())
        {
            NativeScaleoutDescriptor desc {*this,
                                           *m_scaleoutProvider,
                                           currentStream,
                                           m_streamId,
                                           currentStream.getStreamIndex(),
                                           currentStream.getSchedIdx()};
            desc.run(sliceState);
        }
        else if (m_scaleoutProvider->isGaudiDirect())
        {
            GaudiDirectScaleoutDescriptor desc {*this,
                                                *m_scaleoutProvider,
                                                currentStream,
                                                m_streamId,
                                                currentStream.getStreamIndex(),
                                                currentStream.getSchedIdx(),
                                                m_commands};
            desc.run(sliceState);
        }
        else
        {
            LibfabricScaleoutDescriptor desc {*this,
                                              *m_scaleoutProvider,
                                              currentStream,
                                              m_streamId,
                                              currentStream.getStreamIndex(),
                                              currentStream.getSchedIdx(),
                                              m_commands};
            desc.run(sliceState);
        }
    }
}

void HclCollectiveRoutinesGen2Arch::createScaleOutRecvProgs(SliceState& sliceState,
                                                            unsigned    requiredCredits)
{
    hcl::ScalStream& currentStream =
        ActiveStreamManagerGen2Arch::getActiveCollectiveStream(m_deviceController,
                                                               sliceState.m_currentOp,
                                                               m_streamId,
                                                               (unsigned)hcl::SchedulersIndex::recvScaleOut);
    currentStream.setTargetValue(m_longSo.targetValue);

    hcl::ScalStream& arbitratorStream =
        m_deviceController.getScalStream(m_streamId, currentStream.getSchedIdx(), ARB_STREAM_IDX);
    arbitratorStream.setTargetValue(m_longSo.targetValue);

    BarrierArbitratorDescriptor desc {*this,
                                      *m_scaleoutProvider,
                                      currentStream,
                                      arbitratorStream,
                                      m_streamId,
                                      currentStream.getStreamIndex(),
                                      currentStream.getSchedIdx(),
                                      requiredCredits,
                                      m_longSo};
    desc.run(sliceState);

    LOG_HCL_CONTEXT_TRACE(HCL, "Serializing a scaleout recv scheduler commands");

    if (m_signalsManager->isEventRegistered(SignalEvent::SCALEOUT_RECV) ||
        m_signalsManager->isEventRegistered(SignalEvent::HNIC_SCALEOUT_RECV))
    {
        // Needs to wait if the prev box of our slot is at a lower boxIter.
        if (sliceState.m_isReductionCollective && sliceState.m_isMultiScaleupGroup &&
            sliceState.m_boxIter >= sliceState.m_reproScaleoutBuffersAmount)
        {
            int longtermOffset = sliceState.m_boxIter % sliceState.m_reproScaleoutBuffersAmount;

            streamAddSingleWaitIfNeeded(
                currentStream,
                {(WaitEvent)((unsigned)WaitEvent::RR_RS_SO_RECV_WAIT_FOR_PREV_RECV_BASE + longtermOffset)});
        }
        if (!m_scaleoutProvider->isHostNic())
        {
            NativeScaleoutDescriptor desc {*this,
                                           *m_scaleoutProvider,
                                           currentStream,
                                           m_streamId,
                                           currentStream.getStreamIndex(),
                                           currentStream.getSchedIdx()};
            desc.run(sliceState);
        }
        else if (m_scaleoutProvider->isGaudiDirect())
        {
            GaudiDirectScaleoutDescriptor desc {*this,
                                                *m_scaleoutProvider,
                                                currentStream,
                                                m_streamId,
                                                currentStream.getStreamIndex(),
                                                currentStream.getSchedIdx(),
                                                m_commands};
            desc.run(sliceState);
        }
        else
        {
            LibfabricScaleoutDescriptor desc {*this,
                                              *m_scaleoutProvider,
                                              currentStream,
                                              m_streamId,
                                              currentStream.getStreamIndex(),
                                              currentStream.getSchedIdx(),
                                              m_commands};
            desc.run(sliceState);
        }
    }
}

void HclCollectiveRoutinesGen2Arch::createScaleOutSendProgsNonCollective(
    const SendRecvVector&                   sendVec,
    const HCL_Comm                          comm,
    const unsigned                          requiredCredits,
    std::unordered_map<HCL_Rank, unsigned>& qpSetIterPerSendPeerRank,
    const CommonState&                      commonState)
{
    LOG_HCL_TRACE(HCL, "requiredCredits={}, sendVec.size={}", requiredCredits, sendVec.size());
    hcl::ScalStream& currentStream =
        ActiveStreamManagerGen2Arch::getActiveCollectiveStream(m_deviceController,
                                                               eHCLNoCollective,
                                                               m_streamId,
                                                               (unsigned)hcl::SchedulersIndex::sendScaleOut);
    currentStream.setTargetValue(m_longSo.targetValue);

    hcl::ScalStream& arbitratorStream =
        m_deviceController.getScalStream(m_streamId, currentStream.getSchedIdx(), ARB_STREAM_IDX);
    arbitratorStream.setTargetValue(m_longSo.targetValue);

    const bool     isHnicsScaleout = m_scaleoutProvider->isHostNic();
    const uint16_t myBox           = commonState.m_dynamicComm.getMyScaleupGroup();

    const bool isScaleOutRequired = (sendVec.size() > 0);
    LOG_HCL_TRACE(HCL, "isScaleOutRequired={}, isHnicsScaleout={}", isScaleOutRequired, isHnicsScaleout);
    // create a NonCollectiveState for all commands.
    // The barrier arbitrator arm requires it and later on the fences are
    // stored in there for use for entire flow
    NonCollectiveState nonCollectiveSliceState(commonState,
                                               *m_addressGenerator,
                                               true /* isSend*/,
                                               m_graphSync.getCurrentCgSoAddr(CgType::eExternal) /*completionSoAddr*/,
                                               isScaleOutRequired);
    m_scaleoutProvider->requestScaleoutResources(nonCollectiveSliceState);
    provideScaleoutResources(nonCollectiveSliceState);

    LOG_HCL_TRACE(
        HCL,
        "myBox={}, "
        "nonCollectiveSliceState: m_sendBufferAddr=0x{:x}, m_root={}, m_count={}, "
        "m_isMultiScaleupGroup={}, "
        "m_boxIterations={}, m_boxStride={}, m_execution.m_deviceAddress=0x{:x}, m_execution.m_deviceCount={}, "
        "m_execution.m_cellCount={}, "
        "m_hasBufferSize={}, "
        "m_execution.m_completionSoAddr=0x{:x}, "
        "m_setup.m_scaleoutInternalFences={}, m_setup.m_scaleoutInternalSOBs={}",
        myBox,
        nonCollectiveSliceState.m_sendBufferAddr,
        nonCollectiveSliceState.m_root,
        nonCollectiveSliceState.m_count,
        nonCollectiveSliceState.m_isMultiScaleupGroup,
        nonCollectiveSliceState.m_boxIterations,
        nonCollectiveSliceState.m_boxStrideCount,
        nonCollectiveSliceState.m_execution.m_deviceAddress,
        nonCollectiveSliceState.m_execution.m_deviceCount,
        nonCollectiveSliceState.m_execution.m_cellCount,
        nonCollectiveSliceState.m_hasBufferSize,
        nonCollectiveSliceState.m_execution.m_completionSoAddr,
        nonCollectiveSliceState.m_setup.m_scaleoutInternalFences,
        nonCollectiveSliceState.m_setup.m_scaleoutInternalSOBs);

    BarrierArbitratorDescriptor desc {*this,
                                      *m_scaleoutProvider,
                                      currentStream,
                                      arbitratorStream,
                                      m_streamId,                      // archStreamIdx
                                      currentStream.getStreamIndex(),  // uarchStreamIdx
                                      currentStream.getSchedIdx(),     // schedIdx
                                      requiredCredits,
                                      m_longSo};
    desc.run(nonCollectiveSliceState);

    if (!isScaleOutRequired) return;

    bool isFirstRank = true;

    unsigned remoteRanksIter = 0;
    for (const SendRecvEntry& entry : sendVec)
    {
        const HCL_Rank remoteRank = entry.remoteRank;
        LOG_HCL_TRACE(HCL,
                      "remoteRank={}, entry.address=0x{:x}, entry.count={}, isFirstRank={}, remoteRanksIter={}",
                      remoteRank,
                      entry.address,
                      entry.count,
                      isFirstRank,
                      remoteRanksIter);

        NonCollectiveState& sendSliceState(nonCollectiveSliceState);
        const uint16_t      remoteBox = sendSliceState.m_dynamicComm.getRankToScaleupGroupMap()[remoteRank];

        const WqeWraparoundBits wraparoundBits = {false, false};
        const uint64_t          hostMappedAddr =
            (isHnicsScaleout && !m_scaleoutProvider->isGaudiDirect())
                         ? m_scaleoutProvider->getHostBufferManager(m_streamId)->getCurrentMappedBuffer(HNIC_SEND_POOL)
                         : 0;
        const uint64_t hostAddr =
            (isHnicsScaleout && !m_scaleoutProvider->isGaudiDirect())
                ? m_scaleoutProvider->getHostBufferManager(m_streamId)->getCurrentBuffer(HNIC_SEND_POOL)
                : 0;
        sendSliceState.updateState(remoteBox,
                                   remoteRank,
                                   entry.dataType,
                                   entry.address,
                                   entry.count,
                                   isFirstRank,
                                   0 /* recvFenceValue - N/A*/,
                                   hostMappedAddr,
                                   hostAddr);
        if (m_device->getComm(comm).isPeer(remoteRank))
        {
            VERIFY(qpSetIterPerSendPeerRank.count(remoteRank) != 0);
            const unsigned qpSetIter = qpSetIterPerSendPeerRank[remoteRank]++;
            sendSliceState.calcSliceQpSet(qpSetIter);
        }
        else
        {
            sendSliceState.m_qpSet = 0;  // for non peer remotes, do not try to optimize
        }

        LOG_HCL_TRACE(HCL,
                      "remoteBox={}, "
                      "remoteRank={}, "
                      "remoteRanksIter={}, "
                      "sendSliceState:: m_sendBufferAddr=0x{:x}, m_root={}, m_count={}, "
                      "m_isMultiScaleupGroup={}, m_boxIterations={}, m_boxStride={}, m_execution.m_deviceAddress=0x{:x}, "
                      "m_execution.m_deviceCount={}, m_execution.m_cellCount={}, "
                      "m_hasBufferSize={}, m_execution.m_completionSoAddr=0x{:x}, m_firstRank={}, m_qpSet={}",
                      remoteBox,
                      remoteRank,
                      remoteRanksIter,
                      sendSliceState.m_sendBufferAddr,
                      sendSliceState.m_root,
                      sendSliceState.m_count,
                      sendSliceState.m_isMultiScaleupGroup,
                      sendSliceState.m_boxIterations,
                      sendSliceState.m_boxStrideCount,
                      sendSliceState.m_execution.m_deviceAddress,
                      sendSliceState.m_execution.m_deviceCount,
                      sendSliceState.m_execution.m_cellCount,
                      sendSliceState.m_hasBufferSize,
                      sendSliceState.m_execution.m_completionSoAddr,
                      sendSliceState.m_firstRank,
                      sendSliceState.getQpSet());

        // prepare next remoteRank to receive data from
        if (!isHnicsScaleout)
        {
            NativeNonCollectiveScaleoutDescriptor desc {*this,
                                                        *m_scaleoutProvider,
                                                        currentStream,
                                                        m_streamId,
                                                        currentStream.getStreamIndex(),
                                                        currentStream.getSchedIdx(),
                                                        wraparoundBits};
            desc.run(sendSliceState);
        }
        else if (m_scaleoutProvider->isGaudiDirect())
        {
            GaudiDirectNonCollectiveScaleoutDescriptor desc {*this,
                                                             *m_scaleoutProvider,
                                                             currentStream,
                                                             m_streamId,
                                                             currentStream.getStreamIndex(),
                                                             currentStream.getSchedIdx(),
                                                             m_longSo.targetValue,
                                                             m_commands};
            desc.run(sendSliceState);
        }
        else
        {
            LibfabricNonCollectiveScaleoutDescriptor desc {*this,
                                                           *m_scaleoutProvider,
                                                           currentStream,
                                                           m_streamId,
                                                           currentStream.getStreamIndex(),
                                                           currentStream.getSchedIdx(),
                                                           m_longSo.targetValue,
                                                           m_commands};
            desc.run(sendSliceState);
        }
        isFirstRank = false;
        remoteRanksIter++;
    }
}

void HclCollectiveRoutinesGen2Arch::createScaleOutRecvProgsNonCollective(
    const SendRecvVector&                   recvVec,
    const HCL_Comm                          comm,
    const unsigned                          requiredCredits,
    std::unordered_map<HCL_Rank, unsigned>& qpSetIterPerRecvPeerRank,
    const CommonState&                      commonState)
{
    LOG_HCL_TRACE(HCL, "comm={}, requiredCredits={}, recvVec.size={}", comm, requiredCredits, recvVec.size());
    hcl::ScalStream& currentStream =
        ActiveStreamManagerGen2Arch::getActiveCollectiveStream(m_deviceController,
                                                               eHCLNoCollective,
                                                               m_streamId,
                                                               (unsigned)hcl::SchedulersIndex::recvScaleOut);
    currentStream.setTargetValue(m_longSo.targetValue);

    hcl::ScalStream& arbitratorStream =
        m_deviceController.getScalStream(m_streamId, currentStream.getSchedIdx(), ARB_STREAM_IDX);
    arbitratorStream.setTargetValue(m_longSo.targetValue);

    const bool     isHnicsScaleout = m_scaleoutProvider->isHostNic();
    const uint16_t myBox           = commonState.m_dynamicComm.getMyScaleupGroup();
    // create a NonCollectiveState for all commands the barrier arbitrator arm requires it and later on the fences are
    // stored in there for use for entire flow
    const bool isScaleOutRequired = (recvVec.size() > 0);
    LOG_HCL_TRACE(HCL, "isScaleOutRequired={}, isHnicsScaleout={}", isScaleOutRequired, isHnicsScaleout);
    NonCollectiveState nonCollectiveSliceState(commonState,
                                               *m_addressGenerator,
                                               false /* isSend*/,
                                               m_graphSync.getCurrentCgSoAddr(CgType::eExternal) /*completionSoAddr*/,
                                               isScaleOutRequired);
    m_scaleoutProvider->requestScaleoutResources(nonCollectiveSliceState);
    provideScaleoutResources(nonCollectiveSliceState);
    LOG_HCL_TRACE(HCL,
                  "myBox={}, nonCollectiveSliceState: m_recvBufferAddr=0x{:x}, m_root={}, m_count={}, "
                  "m_isMultiScaleupGroup={}, m_boxIterations={}, m_boxStride={}, m_execution.m_deviceAddress=0x{:x}, "
                  "m_execution.m_deviceCount={}, m_execution.m_cellCount={}, "
                  "m_hasBufferSize={}, m_execution.m_completionSoAddr=0x{:x}, "
                  "m_setup.m_scaleoutInternalFences={}, "
                  "m_setup.m_scaleoutInternalSOBs={}, "
                  "m_recvFenceValue={}, m_firstRank={}",
                  myBox,
                  nonCollectiveSliceState.m_recvBufferAddr,
                  nonCollectiveSliceState.m_root,
                  nonCollectiveSliceState.m_count,
                  nonCollectiveSliceState.m_isMultiScaleupGroup,
                  nonCollectiveSliceState.m_boxIterations,
                  nonCollectiveSliceState.m_boxStrideCount,
                  nonCollectiveSliceState.m_execution.m_deviceAddress,
                  nonCollectiveSliceState.m_execution.m_deviceCount,
                  nonCollectiveSliceState.m_execution.m_cellCount,
                  nonCollectiveSliceState.m_hasBufferSize,
                  nonCollectiveSliceState.m_execution.m_completionSoAddr,
                  nonCollectiveSliceState.m_setup.m_scaleoutInternalFences,
                  nonCollectiveSliceState.m_setup.m_scaleoutInternalSOBs,
                  nonCollectiveSliceState.m_recvFenceValue,
                  nonCollectiveSliceState.m_firstRank);

    BarrierArbitratorDescriptor desc {*this,
                                      *m_scaleoutProvider,
                                      currentStream,
                                      arbitratorStream,
                                      m_streamId,                      // archStreamIdx
                                      currentStream.getStreamIndex(),  // uarchStreamIdx
                                      currentStream.getSchedIdx(),     // currentStream.getSchedIdx()
                                      requiredCredits,
                                      m_longSo};
    desc.run(nonCollectiveSliceState);

    if (!isScaleOutRequired) return;

    const unsigned int recvFenceValue = recvVec.size();  // we currently have 1 monitor for this stream, so we
                                                         // block until all recv are done and then do the PDMA
    bool isFirstRank = true;
    for (const SendRecvEntry& entry : recvVec)
    {
        m_wqeTracker->incWqe(comm,
                             entry.remoteRank / m_device->getComm(comm).getScaleupGroupSize(),
                             currentStream.getStreamIndex() == 0 ? QpType::ScaleOutReduceScatter
                                                                 : QpType::ScaleOutAllGather);
    }

    unsigned remoteRanksIter = 0;
    for (const SendRecvEntry& entry : recvVec)
    {
        const WqeWraparoundBits wraparoundBits = m_wqeTracker->getWqeWraparoundBits(
            comm,
            entry.remoteRank / m_device->getComm(comm).getScaleupGroupSize(),
            currentStream.getStreamIndex() == 0 ? QpType::ScaleOutReduceScatter : QpType::ScaleOutAllGather);

        const HCL_Rank remoteRank = entry.remoteRank;
        LOG_HCL_TRACE(HCL,
                      "remoteRank={}, entry.address=0x{:x}, entry.count={}, recvFenceValue={}, isFirstRank={}, "
                      "remoteRanksIter={}",
                      remoteRank,
                      entry.address,
                      entry.count,
                      recvFenceValue,
                      isFirstRank,
                      remoteRanksIter);

        NonCollectiveState& recvSliceState(nonCollectiveSliceState);
        const uint16_t      remoteBox = recvSliceState.m_dynamicComm.getRankToScaleupGroupMap()[remoteRank];

        const uint64_t hostMappedAddr =
            (isHnicsScaleout && !m_scaleoutProvider->isGaudiDirect())
                ? m_scaleoutProvider->getHostBufferManager(m_streamId)->getCurrentMappedBuffer(HNIC_RECV_POOL)
                : 0;
        const uint64_t hostAddr =
            (isHnicsScaleout && !m_scaleoutProvider->isGaudiDirect())
                ? m_scaleoutProvider->getHostBufferManager(m_streamId)->getCurrentBuffer(HNIC_RECV_POOL)
                : 0;
        recvSliceState.updateState(remoteBox,
                                   remoteRank,
                                   entry.dataType,
                                   entry.address,
                                   entry.count,
                                   isFirstRank,
                                   recvFenceValue,
                                   hostMappedAddr,
                                   hostAddr);
        if (m_device->getComm(comm).isPeer(remoteRank))
        {
            VERIFY(qpSetIterPerRecvPeerRank.count(remoteRank) != 0);
            const unsigned qpSetIter = qpSetIterPerRecvPeerRank[remoteRank]++;
            recvSliceState.calcSliceQpSet(qpSetIter);
        }
        else
        {
            recvSliceState.m_qpSet = 0;  // for non peer remotes, do not try to optimize
        }

        LOG_HCL_TRACE(HCL,
                      "remoteBox={}, "
                      "remoteRank={}, "
                      "remoteRanksIter={}, "
                      "recvSliceState:: m_recvBufferAddr=0x{:x}, m_root={}, m_count={}, "
                      "m_isMultiScaleupGroup={}, m_boxIterations={}, m_boxStride={}, m_execution.m_deviceAddress=0x{:x}, "
                      "m_execution.m_deviceCount={}, m_execution.m_cellCount={}, "
                      "m_hasBufferSize={}, m_execution.m_completionSoAddr=0x{:x}, m_recvFenceValue={}, "
                      "m_firstRank={}, m_qpSet={}, "
                      "wraparoundBits.notify_rndv_ack={}, wraparoundBits.wait_for_rndv_acks={}",
                      remoteBox,
                      remoteRank,
                      remoteRanksIter,
                      recvSliceState.m_recvBufferAddr,
                      recvSliceState.m_root,
                      recvSliceState.m_count,
                      recvSliceState.m_isMultiScaleupGroup,
                      recvSliceState.m_boxIterations,
                      recvSliceState.m_boxStrideCount,
                      recvSliceState.m_execution.m_deviceAddress,
                      recvSliceState.m_execution.m_deviceCount,
                      recvSliceState.m_execution.m_cellCount,
                      recvSliceState.m_hasBufferSize,
                      recvSliceState.m_execution.m_completionSoAddr,
                      recvSliceState.m_recvFenceValue,
                      recvSliceState.m_firstRank,
                      recvSliceState.getQpSet(),
                      wraparoundBits.notify_rndv_ack,
                      wraparoundBits.wait_for_rndv_acks);

        // prepare next remoteRank to receive data from
        if (!isHnicsScaleout)
        {
            NativeNonCollectiveScaleoutDescriptor desc {*this,
                                                        *m_scaleoutProvider,
                                                        currentStream,
                                                        m_streamId,
                                                        currentStream.getStreamIndex(),
                                                        currentStream.getSchedIdx(),
                                                        wraparoundBits};
            desc.run(recvSliceState);
        }
        else if (m_scaleoutProvider->isGaudiDirect())
        {
            GaudiDirectNonCollectiveScaleoutDescriptor desc {*this,
                                                             *m_scaleoutProvider,
                                                             currentStream,
                                                             m_streamId,
                                                             currentStream.getStreamIndex(),
                                                             currentStream.getSchedIdx(),
                                                             m_longSo.targetValue,
                                                             m_commands};
            desc.run(recvSliceState);
        }
        else
        {
            LibfabricNonCollectiveScaleoutDescriptor desc {*this,
                                                           *m_scaleoutProvider,
                                                           currentStream,
                                                           m_streamId,
                                                           currentStream.getStreamIndex(),
                                                           currentStream.getSchedIdx(),
                                                           m_longSo.targetValue,
                                                           m_commands};
            desc.run(recvSliceState);
        }
        isFirstRank = false;
        remoteRanksIter++;
    }
}
