#include "platform/gen2_arch_common/hcl_device_controller.h"
#include "infra/scal/gen2_arch_common/scal_manager.h"         // for Gen2Arc...
#include "platform/gen2_arch_common/commands/hcl_commands.h"  // for HclComm...
#include "platform/gen2_arch_common/hcl_device.h"             // for HclDevi...
#include "platform/gen2_arch_common/hcl_packets_utils.h"      // for SoBaseAndSize, getCompCfg
#include "infra/scal/gen2_arch_common/scal_names.h"
#include "infra/scal/gen2_arch_common/scal_utils.h"

HclDeviceControllerGen2Arch::HclDeviceControllerGen2Arch(const unsigned numOfStreams) : m_numOfStreams(numOfStreams)
{
    m_graphSync        = std::make_unique<std::unique_ptr<HclGraphSyncGen2Arch>[]>(m_numOfStreams);
    m_streamSyncParams = new ArchStreamSyncParams[m_numOfStreams];

    for (unsigned i = 0; i < m_numOfStreams; i++)
    {
        m_streamSyncParams[i].m_longtermGPSOManager   = new CreditManager(GCFG_HCL_LONGTERM_GPSO_COUNT.value());
        m_streamSyncParams[i].m_requestedExtraCredits = 0;
    }
}

HclDeviceControllerGen2Arch::~HclDeviceControllerGen2Arch()
{
    for (unsigned i = 0; i < m_numOfStreams; i++)
    {
        if (m_streamSyncParams[i].m_regularGPSOManager != nullptr) delete m_streamSyncParams[i].m_regularGPSOManager;
        if (m_streamSyncParams[i].m_longtermGPSOManager != nullptr) delete m_streamSyncParams[i].m_longtermGPSOManager;
    }

    delete[] m_streamSyncParams;
}

HclCommandsGen2Arch& HclDeviceControllerGen2Arch::getGen2ArchCommands()
{
    return *m_commands;
}

hcl::Gen2ArchScalManager& HclDeviceControllerGen2Arch::getGen2ArchScalManager()
{
    return *m_scalManager;
}

void HclDeviceControllerGen2Arch::setupCompCfg(int archStreamId)
{
    LOG_HCL_TRACE(HCL, "Stream({})", archStreamId);
    uint32_t soBase = 0xffffffff;
    size_t   soSize = 0;
    for (unsigned gpIdx = 0; gpIdx < (unsigned)GpsoPool::COUNT; gpIdx++)
    {
        soBase = soBase > m_graphSync[archStreamId]->getSoPoolBaseAddr(gpIdx)
                     ? m_graphSync[archStreamId]->getSoPoolBaseAddr(gpIdx)
                     : soBase;
        soSize += m_graphSync[archStreamId]->getSoPoolSize(gpIdx);
    }
    getCompCfg()[archStreamId * 2] =
        SoBaseAndSize(m_scalManager->getCgInfo(archStreamId)[(int)hcl::SchedulerType::external].cgBaseAddr,
                      m_scalManager->getCgInfo(archStreamId)[(int)hcl::SchedulerType::external].size);
    getCompCfg()[archStreamId * 2 + 1] = SoBaseAndSize(soBase, soSize);
}

void HclDeviceControllerGen2Arch::initDeviceForCollectiveRoutine(int            archStreamId,
                                                                 hcl::syncInfo* longSo,
                                                                 hcl::syncInfo* longSoNullSubmit)
{
    LOG_HCL_TRACE(HCL,
                  "Stream({}), LongSO({}, {}, 0x{:x})",
                  archStreamId,
                  longSo->long_so_index,
                  longSo->targetValue,
                  (uint64_t)(longSo->cp_handle));
    auto& syncParams                = getSyncParams(archStreamId);
    syncParams.m_longSo             = longSo;
    syncParams.m_longSoNullSubmit   = longSoNullSubmit;
    auto& smInfo                    = syncParams.m_smInfo;
    auto& intermediateBufferManager = m_device->getSIB(archStreamId);
    m_graphSync[archStreamId]->setSyncData(smInfo.soBaseIdx, smInfo.soSize);

    std::array<hcl::CgInfo, (int)hcl::SchedulerType::count> cgInfo = m_scalManager->getCgInfo(archStreamId);
    m_graphSync[archStreamId]->setCgInfo(cgInfo[(int)hcl::SchedulerType::external],
                                         cgInfo[(int)hcl::SchedulerType::internal],
                                         GCFG_HCL_LONGTERM_GPSO_COUNT.value(),
                                         intermediateBufferManager.getPoolBufferSize(SCALEUP_AND_ALL2ALL_POOL));
    longSo->long_so_index = cgInfo[(int)hcl::SchedulerType::external].longSoIndex;
    longSo->targetValue   = cgInfo[(int)hcl::SchedulerType::external].longSoInitialValue;
    longSo->cp_handle     = m_scalManager->getCgHandle(archStreamId, true);

    LOG_HCL_TRACE(HCL,
                  "Initialized LongSO({}, {}, 0x{:x})",
                  longSo->long_so_index,
                  longSo->targetValue,
                  (uint64_t)(longSo->cp_handle));

    *longSoNullSubmit = *longSo;

    syncParams.m_regularGPSOManager = new CreditManager(m_graphSync[archStreamId]->getSoPoolSize(GpsoPool::GPSO_0));

    for (unsigned poolSizeIndex = 0; poolSizeIndex < intermediateBufferManager.getPoolAmount(); poolSizeIndex++)
    {
        m_scalManager->addStaticBufferAddrAndSize(intermediateBufferManager.getBufferBaseAddr(poolSizeIndex),
                                                  intermediateBufferManager.getSingleBufferSize(poolSizeIndex),
                                                  intermediateBufferManager.getBufferAmountInPool(poolSizeIndex));
    }

    const unsigned monitorsPerSched     = smInfo.monitorSize / TOTAL_SCHED_NR;
    const unsigned longMonitorsPerSched = ((smInfo.longMonitorSize / 4) / TOTAL_SCHED_NR) * 4;

    for (unsigned i = 0; i < SCHED_NR; ++i)
    {
        syncParams.m_schedulers[i].internalResources.monitorBase  = smInfo.monitorBaseIdx + (i * monitorsPerSched);
        syncParams.m_schedulers[i].internalResources.monitorsSize = monitorsPerSched;
        syncParams.m_schedulers[i].internalResources.longMonitorBase =
            smInfo.longMonitorBaseIdx + (i * longMonitorsPerSched);
        syncParams.m_schedulers[i].internalResources.longMonitorsSize = longMonitorsPerSched;
    }

    // To ensure external and internal Cgs are synced, we allocate all the SOs in the external Cg
    allocAllExternalBarrier(archStreamId);

    // setup all monitors once
    setupMonitors(archStreamId);

    setupCompCfg(archStreamId);
}

void HclDeviceControllerGen2Arch::allocAllExternalBarrier(int archStreamId)
{
    hcl::CgInfo      externalCgInfo = m_graphSync[archStreamId]->getCgData(true);
    unsigned         schedIdx       = (unsigned)hcl::SchedulersIndex::dma;
    hcl::ScalStream& currentStream  = m_scalManager->getScalStream(archStreamId, schedIdx, 0);
    currentStream.setTargetValue(m_streamSyncParams[archStreamId].m_longSo->targetValue);
    m_commands->serializeAllocBarrierCommand(currentStream,
                                             schedIdx,
                                             externalCgInfo.cgIdx[(int)hcl::SchedulersIndex::dma],
                                             externalCgInfo.size);
}

void HclDeviceControllerGen2Arch::setupMonitors(int archStreamId)
{
    // setup monitors for each scheduler
    for (unsigned schedIdx = 0; schedIdx < (unsigned)hcl::SchedulersIndex::count; ++schedIdx)
    {
        auto& schedResources = m_streamSyncParams[archStreamId].m_schedulers[schedIdx].internalResources;

        // Make sure we have enough monitors
        VERIFY(schedResources.monitorsSize >= MONITORS_PER_STREAM * STREAMS_NR, "There aren't enough monitors");
        // Make sure we have enough long monitors
        VERIFY(schedResources.longMonitorsSize >= LONG_MONITOR_LENGTH * LONG_MONITORS_PER_STREAM * STREAMS_NR,
               "There aren't enough long monitors");

        for (unsigned uarchStreamId = 0; uarchStreamId < m_scalManager->getMicroArchStreams(schedIdx); ++uarchStreamId)
        {
            // We set the setup on the first stream of the dma
            unsigned         streamForCommands = schedIdx == 0 ? 0 : uarchStreamId;
            hcl::ScalStream& currentStream = m_scalManager->getScalStream(archStreamId, schedIdx, streamForCommands);
            unsigned         fenceBase     = getFenceIdx(archStreamId, uarchStreamId, FENCE_MONITOR_IDX);

            currentStream.setTargetValue(m_streamSyncParams[archStreamId].m_longSo->targetValue);
            for (unsigned fenceIdx = 0; fenceIdx < FENCES_PER_STREAM; ++fenceIdx)
            {
                uint64_t monitorPayloadAddr =
                    m_scalManager->getMonitorPayloadAddr((hcl::SchedulersIndex)schedIdx, fenceBase + fenceIdx);

                m_graphSync[archStreamId]->addSetupMonitors(currentStream,
                                                            //  schedIdx,
                                                            uarchStreamId,
                                                            schedResources.monitorBase,
                                                            m_streamSyncParams[archStreamId].m_smInfo.monitorSmIndex,
                                                            monitorPayloadAddr,
                                                            fenceBase,
                                                            fenceIdx);
            }

            for (unsigned fenceIdx = 0; fenceIdx < LONG_MONITORS_PER_STREAM; ++fenceIdx)
            {
                uint64_t monitorPayloadAddr =
                    m_scalManager->getMonitorPayloadAddr((hcl::SchedulersIndex)schedIdx, fenceBase + fenceIdx);

                m_graphSync[archStreamId]->addSetupLongMonitors(
                    currentStream,
                    m_streamSyncParams[archStreamId].m_smInfo.longMonitorSmIndex,
                    monitorPayloadAddr,
                    getLongMonitorIdx(archStreamId, schedIdx, uarchStreamId),
                    fenceBase,
                    fenceIdx);
            }
        }
    }
}

void HclDeviceControllerGen2Arch::advanceProg(int archStreamId, bool nopOp)
{
    m_streamSyncParams[archStreamId].m_longSo->targetValue++;

    if (!GCFG_HCL_NULL_SUBMIT.value() || nopOp)
    {
        m_graphSync[archStreamId]->incSoIndex(1);
    }

    m_device->getSIB(archStreamId).advanceProg(m_streamSyncParams[archStreamId].m_longSo->targetValue);

    m_streamSyncParams[archStreamId].m_longtermGPSOManager->advanceProg(
        m_streamSyncParams[archStreamId].m_longSo->targetValue);

    // Indicates that we had an api call after event wait, so in the next event record Nop is not needed.
    m_streamSyncParams[archStreamId].m_isPrevWaitEvent = false;
}

unsigned int HclDeviceControllerGen2Arch::handleExtraCredits(int archStreamId, unsigned extraCreditsNeeded)
{
    unsigned creditsToAsk = 1;
    auto&    syncParams   = getSyncParams(archStreamId);
    if (extraCreditsNeeded >= syncParams.m_requestedExtraCredits)
    {
        creditsToAsk += extraCreditsNeeded - syncParams.m_requestedExtraCredits;
        syncParams.m_requestedExtraCredits += (extraCreditsNeeded - syncParams.m_requestedExtraCredits);
    }
    else if (syncParams.m_requestedExtraCredits)
    {
        --syncParams.m_requestedExtraCredits;
        creditsToAsk = 0;
    }

    LOG_HCL_TRACE(HCL, "extraCreditsNeeded={}, creditsToAsk={}", extraCreditsNeeded, creditsToAsk);
    return creditsToAsk;
}

void HclDeviceControllerGen2Arch::addNop(int archStreamId)
{
    advanceProg(archStreamId, true);
    auto& syncParams = getSyncParams(archStreamId);
    syncParams.m_longSoNullSubmit->targetValue++;

    uint64_t targetValue =
        GCFG_HCL_NULL_SUBMIT.value() ? syncParams.m_longSoNullSubmit->targetValue : syncParams.m_longSo->targetValue;

    LOG_HCL_CONTEXT_INFO(HCL, "Running Nop command, targetValue={}", syncParams.m_longSo->targetValue);

    LOG_TRACE(HCL_CG,
              SCAL_PROGRESS_HCL_FMT "addNop",
              archStreamId,
              syncParams.m_longSo->long_so_index,
              syncParams.m_longSo->targetValue);

    const unsigned requiredCredits = GCFG_HCL_NULL_SUBMIT.value() ? 1 : handleExtraCredits(archStreamId, 0);

    // the long SO and the GPSO pool are tightly coupled so we need to move the gpso idx
    syncParams.m_regularGPSOManager->allocNextCredit(syncParams.m_longSo->targetValue);

    unsigned schedIdx               = (unsigned)hcl::SchedulersIndex::dma;
    auto&    arbStream              = m_scalManager->getScalStream(archStreamId, schedIdx, ARB_STREAM_IDX);
    auto&    garbageCollectorStream = m_scalManager->getScalStream(archStreamId, schedIdx, 0);
    arbStream.setTargetValue(targetValue);
    garbageCollectorStream.setTargetValue(targetValue);

    addBarrierArm(arbStream, false, requiredCredits, {0});

    int additionalSignalInternal = m_graphSync[archStreamId]->isForceOrder(false) ? 1 : 0;
    waitForBarrierArm(garbageCollectorStream);
    addBarrierArm(garbageCollectorStream, true, 1, {});

    m_commands->serializeLbwWriteCommand(
        garbageCollectorStream,
        schedIdx,
        m_graphSync[archStreamId]->getCurrentCgSoAddr(CgType::eInternal),
        m_graphSync[archStreamId]->getSoConfigValue(m_scalManager->getCMaxTargetValue() - additionalSignalInternal,
                                                    true));
    incInternalCgTargetValue(archStreamId);

    schedIdx                   = (unsigned)hcl::SchedulersIndex::sendScaleUp;
    auto& sendStream           = m_scalManager->getScalStream(archStreamId, schedIdx, 0);
    auto& arbitratorStreamSend = m_scalManager->getScalStream(archStreamId, schedIdx, ARB_STREAM_IDX);
    sendStream.setTargetValue(targetValue);
    arbitratorStreamSend.setTargetValue(targetValue);

    addBarrierArm(arbitratorStreamSend, false, requiredCredits, {0});
    waitForBarrierArm(sendStream);
    int additionalSignalExternal = m_graphSync[archStreamId]->isForceOrder(true) ? 1 : 0;

    m_commands->serializeLbwWriteCommand(
        sendStream,
        schedIdx,
        m_graphSync[archStreamId]->getCurrentCgSoAddr(CgType::eExternal),
        m_graphSync[archStreamId]->getSoConfigValue(m_scalManager->getCMaxTargetValue() - additionalSignalExternal,
                                                    true));

    schedIdx                   = (unsigned)hcl::SchedulersIndex::recvScaleUp;
    auto& recvStream           = m_scalManager->getScalStream(archStreamId, schedIdx, 0);
    auto& arbitratorStreamRecv = m_scalManager->getScalStream(archStreamId, schedIdx, ARB_STREAM_IDX);
    recvStream.setTargetValue(targetValue);
    arbitratorStreamRecv.setTargetValue(targetValue);
    addBarrierArm(arbitratorStreamRecv, false, requiredCredits, {0});
    waitForBarrierArm(recvStream);

    schedIdx                      = (unsigned)hcl::SchedulersIndex::sendScaleOut;
    auto& sendOutStream           = m_scalManager->getScalStream(archStreamId, schedIdx, 0);
    auto& arbitratorStreamSendOut = m_scalManager->getScalStream(archStreamId, schedIdx, ARB_STREAM_IDX);
    sendOutStream.setTargetValue(targetValue);
    arbitratorStreamSendOut.setTargetValue(targetValue);
    addBarrierArm(arbitratorStreamSendOut, false, requiredCredits, {0});
    waitForBarrierArm(sendOutStream);

    schedIdx                      = (unsigned)hcl::SchedulersIndex::recvScaleOut;
    auto& recvOutStream           = m_scalManager->getScalStream(archStreamId, schedIdx, 0);
    auto& arbitratorStreamRecvOut = m_scalManager->getScalStream(archStreamId, schedIdx, ARB_STREAM_IDX);
    recvOutStream.setTargetValue(targetValue);
    arbitratorStreamRecvOut.setTargetValue(targetValue);
    addBarrierArm(arbitratorStreamRecvOut, false, requiredCredits, {0});
    waitForBarrierArm(recvOutStream);
}

void HclDeviceControllerGen2Arch::submitWork(int archStreamId, bool submitToHw)
{
    auto& syncParams = getSyncParams(archStreamId);

    if (submitToHw)
    {
        for (unsigned schedIdx = 0; schedIdx < SCHED_NR; ++schedIdx)
        {
            for (unsigned uarchStreamId = 0; uarchStreamId < m_scalManager->getMicroArchStreams(schedIdx);
                 ++uarchStreamId)
            {
                hcl::ScalStream& scalStream = m_scalManager->getScalStream(archStreamId, schedIdx, uarchStreamId);
                if (scalStream.requiresSubmission())
                {
                    scalStream.submit();
                }
            }
        }

        uint64_t targetValue = GCFG_HCL_NULL_SUBMIT.value() ? syncParams.m_longSoNullSubmit->targetValue
                                                            : syncParams.m_longSo->targetValue;
        if (targetValue - syncParams.m_submittedTargetValue)
        {
            // External CG
            scal_completion_group_set_expected_ctr(m_scalManager->getCgHandle(archStreamId, true), targetValue);
        }
        syncParams.m_submittedTargetValue = syncParams.m_longSo->targetValue;

        if (syncParams.m_InternalCgTargetValue - syncParams.m_submittedInternalCgTargetValue)
        {
            // Internal CG
            scal_completion_group_set_expected_ctr(m_scalManager->getCgHandle(archStreamId, false),
                                                   syncParams.m_InternalCgTargetValue);
        }
        syncParams.m_submittedInternalCgTargetValue = syncParams.m_InternalCgTargetValue;
    }

    syncParams.m_signalFinalize();
}

void HclDeviceControllerGen2Arch::streamAddWait(hcl::ScalStream&            scalStream,
                                                const SyncObjectDescriptor& descriptor,
                                                bool                        useEqual)
{
    unsigned       archStreamIdx = scalStream.getArchStreamIndex();
    const unsigned waitingMonitorBase =
        m_streamSyncParams[archStreamIdx].m_schedulers[scalStream.getSchedIdx()].internalResources.monitorBase;

    m_graphSync[archStreamIdx]->createSyncStreamsMessages(
        scalStream,
        waitingMonitorBase,
        m_streamSyncParams[archStreamIdx].m_smInfo.soSmIndex,
        descriptor.value,
        descriptor.sob.sobId,
        getFenceIdx(archStreamIdx, scalStream.getUarchStreamIndex(), FENCE_MONITOR_IDX),
        useEqual);
}

void HclDeviceControllerGen2Arch::addBarrierArm(
    hcl::ScalStream&                                               scalStream,
    bool                                                           external,
    unsigned                                                       creditsNr,
    const llvm_vecsmall::SmallVector<unsigned, MAX_STREAM_TO_INC>& streamsToInc,
    bool                                                           shouldAddWait,
    LBWBurstData_t*                                                lbwBurstData)

{
    unsigned                                                archStreamIdx = scalStream.getArchStreamIndex();
    unsigned                                                schedIdx      = scalStream.getSchedIdx();
    llvm_vecsmall::SmallVector<unsigned, MAX_STREAM_TO_INC> fences;

    if (shouldAddWait)
    {
        addStreamWaitOnLongSo(scalStream, ARB_STREAM_IDX);
    }

    if (creditsNr)
    {
        const hcl::CgInfo& cgInfo = m_graphSync[archStreamIdx]->getCgData(external);
        for (unsigned i = 0; i < streamsToInc.size(); i++)
        {
            fences.push_back(getFenceIdx(archStreamIdx, streamsToInc[i], FENCE_BARRIER_IDX));
        }

        m_commands->serializeAllocBarrierCommand(scalStream,
                                                 schedIdx,
                                                 cgInfo.cgIdx[schedIdx],
                                                 creditsNr,
                                                 &fences,
                                                 lbwBurstData);
    }
    else
    {
        for (unsigned i = 0; i < streamsToInc.size(); i++)
        {
            m_commands->serializeFenceIncCommand(scalStream,
                                                 schedIdx,
                                                 getFenceIdx(archStreamIdx, streamsToInc[i], FENCE_BARRIER_IDX));
        }
    }
}

void HclDeviceControllerGen2Arch::waitForBarrierArm(hcl::ScalStream& scalStream)
{
    m_commands->serializeFenceDecCommand(
        scalStream,
        scalStream.getSchedIdx(),
        getFenceIdx(scalStream.getArchStreamIndex(), scalStream.getUarchStreamIndex(), FENCE_BARRIER_IDX));
}

void HclDeviceControllerGen2Arch::addStreamWaitOnLongSo(hcl::ScalStream& scalStream, unsigned uarchStreamId)
{
    unsigned archStreamIdx = scalStream.getArchStreamIndex();
    unsigned schedIdx      = scalStream.getSchedIdx();
    // Apply the pending waits that were requested in previous eventWait calls.
    m_graphSync[archStreamIdx]->addStreamWaitOnLongSo(
        scalStream,
        archStreamIdx,
        m_streamSyncParams[archStreamIdx].m_smInfo.longMonitorSmIndex,
        getLongMonitorIdx(archStreamIdx, schedIdx, uarchStreamId),
        m_streamSyncParams[archStreamIdx].m_schedulers[schedIdx].streams[uarchStreamId],
        getFenceIdx(archStreamIdx, uarchStreamId, FENCE_MONITOR_IDX));
}

void HclDeviceControllerGen2Arch::addStreamWaitOnLongSo(hcl::ScalStream& scalStream, uint64_t soValue, unsigned soIdx)
{
    unsigned archStreamIdx = scalStream.getArchStreamIndex();
    unsigned schedIdx      = scalStream.getSchedIdx();
    unsigned uarchStreamId = scalStream.getUarchStreamIndex();

    m_graphSync[archStreamIdx]->addStreamWaitOnLongSo(scalStream,
                                                      m_streamSyncParams[archStreamIdx].m_smInfo.longMonitorSmIndex,
                                                      getLongMonitorIdx(archStreamIdx, schedIdx, uarchStreamId),
                                                      soValue,
                                                      soIdx,
                                                      getFenceIdx(archStreamIdx, uarchStreamId, FENCE_MONITOR_IDX));
}

void HclDeviceControllerGen2Arch::setHostFences(int     archStreamId,
                                                int     uarchStreamId,
                                                bool    isSend,
                                                uint8_t scaleoutInternalFences,
                                                llvm_vecsmall::SmallVector<FenceInfo, HOST_FENCES_NR>& scaleoutFences)
{
    /*
        ----------------------------------------------
        |      | uarchStreamIdx=0 | uarchStreamIdx=1 |
        ----------------------------------------------
        | Send | Fence=0,4,8,12   | Fence=2,6,10,14  |
        ----------------------------------------------
        | Recv | Fence=1,5,9,13   | Fence=3,7,11,15  |
        ----------------------------------------------
     */
    VERIFY(scaleoutInternalFences <= 1, "Can only provide 1 Host fence per send/recv slice");
    for (uint8_t i = 0; i < scaleoutInternalFences; i++)
    {
        const unsigned fenceIndex = (isSend ? 0 : 1) + 2 * uarchStreamId;
        VERIFY(fenceIndex < HOST_FENCES_NR, "Illegal host fence index");
        hcl::HostFenceInfo& fenceInfo = m_scalManager->getHostFenceInfo(archStreamId, fenceIndex).hostFenceInfo;
        LOG_HCL_TRACE(HCL,
                      "archStream={}, uarchstreamIdx={}, fenceIndex={}, smDcore={}, smIndex={},",
                      archStreamId,
                      uarchStreamId,
                      fenceIndex,
                      fenceInfo.smDcore,
                      fenceInfo.smIndex);
        scaleoutFences.push_back(
            {fenceIndex,
             {m_graphSync[archStreamId]->getRegSobObj(m_graphSync[archStreamId]->getSyncManagerBase(fenceInfo.smDcore),
                                                      fenceInfo.smIndex),
              m_graphSync[archStreamId]->getSoConfigValue(1, true)}});
    }
}

hcl::syncInfo HclDeviceControllerGen2Arch::eventRecord(int      archStreamId,
                                                       bool     isCollectTime /*= false*/,
                                                       uint64_t timestampHandle /*= 0*/,
                                                       uint32_t timestampsOffset /*= 0*/)
{
    auto&                       syncParams = getSyncParams(archStreamId);
    std::lock_guard<std::mutex> lock(syncParams.m_streamLock);
    if (syncParams.m_isPrevWaitEvent || GCFG_HCL_NULL_SUBMIT.value())
    {
        addNop(archStreamId);
        submitWork(archStreamId);
    }
    LOG_TRACE(HCL_CG,
              SCAL_PROGRESS_HCL_FMT "eventRecord",
              archStreamId,
              syncParams.m_longSo->long_so_index,
              syncParams.m_longSo->targetValue);

    hcl::syncInfo longSo = GCFG_HCL_NULL_SUBMIT.value() ? *syncParams.m_longSoNullSubmit : *syncParams.m_longSo;

    if (isCollectTime)
    {
        m_scalManager->cgRegisterTimeStemp(archStreamId, longSo.targetValue, timestampHandle, timestampsOffset);
    }

    syncParams.m_isPrevWaitEvent = false;

    return longSo;
}

void HclDeviceControllerGen2Arch::streamWaitEvent(int archStreamId, hcl::syncInfo commonState)
{
    std::lock_guard<std::mutex> lock(m_streamSyncParams[archStreamId].m_streamLock);

    LOG_TRACE(HCL_CG,
              SCAL_PROGRESS_HCL_FMT "streamWaitEvent",
              archStreamId,
              commonState.long_so_index,
              commonState.targetValue);

    m_streamSyncParams[archStreamId].m_isPrevWaitEvent = true;
    m_graphSync[archStreamId]->addPendingWait(commonState.long_so_index, commonState.targetValue);
}

void HclDeviceControllerGen2Arch::synchronizeStream(int archStreamId)
{
    hcl::syncInfo longSo = eventRecord(archStreamId);
    m_scalManager->synchronizeStream(archStreamId, longSo.targetValue);
}

bool HclDeviceControllerGen2Arch::streamQuery(int archStreamId)
{
    hcl::syncInfo longSo = eventRecord(archStreamId);
    return m_scalManager->streamQuery(archStreamId, longSo.targetValue);
}

void HclDeviceControllerGen2Arch::enableNullSubmit(int archStreamId, bool enable)
{
    m_scalManager->disableCcb(archStreamId, enable);
    m_graphSync[archStreamId]->setNullSubmit(enable);
}

void HclDeviceControllerGen2Arch::setTraceMarker(int archStreamId, uint32_t val)
{
    LOG_HCL_TRACE(HCL, "setTraceMarker = {}", val);
    unsigned         uArchStream = static_cast<unsigned int>(hcl::NetworkStreams::arbitrator);
    hcl::ScalStream& currentStream =
        m_scalManager->getScalStream(archStreamId, (unsigned)hcl::SchedulersIndex::sendScaleUp, uArchStream);
    addStreamWaitOnLongSo(currentStream, uArchStream);

    m_commands->serializeSetTraceMarker(currentStream, currentStream.getSchedIdx(), val);
    submitWork(archStreamId);
}

void HclDeviceControllerGen2Arch::setTraceMarker(int          archStreamId,
                                                 unsigned int schedIdx,
                                                 unsigned int uArchStream,
                                                 uint32_t     val)
{
    LOG_HCL_TRACE(HCL, "setTraceMarker:{}, schedIdx:{}, uArchStream:{}", val, schedIdx, uArchStream);
    hcl::ScalStream& currentStream = m_scalManager->getScalStream(archStreamId, schedIdx, uArchStream);

    m_commands->serializeSetTraceMarker(currentStream, currentStream.getSchedIdx(), val);
}