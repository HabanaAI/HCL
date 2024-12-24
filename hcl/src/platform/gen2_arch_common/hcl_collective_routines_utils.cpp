#include "platform/gen2_arch_common/hcl_collective_routines.h"

#include <ext/alloc_traits.h>  // for __alloc
#include <algorithm>           // for max
#include <cstdint>             // for uint64_t
#include <string>              // for string

#include "interfaces/hcl_unique_sorted_vector.h"              // for UniqueS...
#include "hcl_log_manager.h"                                  // for LOG_*
#include "platform/gen2_arch_common/commands/hcl_commands.h"  // for HclComm...
#include "platform/gen2_arch_common/signals/types.h"
#include "platform/gen2_arch_common/collective_states.h"
#include "hcl_utils.h"  // for VERIFY
#include "platform/gen2_arch_common/device_buffer_manager.h"
#include "intermediate_buffer_container.h"
#include "platform/gen2_arch_common/host_buffer_manager.h"  // for HostBufferManager
#include "platform/gen2_arch_common/hcl_graph_sync.h"       // for HclGraphSyncGen2Arch
#include "platform/gen2_arch_common/signals/manager.h"      // for SignalsManager
#include "platform/gen2_arch_common/collective_utils.h"     // for getNextBox, getPrevBox
#include "platform/gen2_arch_common/dependency_checker.h"   // for DependencyChecker
#include "platform/gen2_arch_common/active_stream_manager.h"
#include "platform/gen2_arch_common/scaleout_provider.h"  // for ScaleoutProvider
#include "platform/gen2_arch_common/hcl_device.h"         // for HclDevi...
#include "hcl_math_utils.h"

void HclCollectiveRoutinesGen2Arch::determineCompletionSO(SliceState& sliceState, bool isFirstBox, bool isLastBox)
{
    sliceState.m_execution.m_scaleoutCompletionWaitEvent  = WaitEvent::GENERAL_COMPLETION_EVENT;
    sliceState.m_execution.m_scaleoutCompletionWaitMethod = WaitMethod::EXTERNAL_CG_SO;

    if (sliceState.m_isSend)
    {
        if (sliceState.m_syncUpBufferWithLtu && !isFirstBox)
        {
            sliceState.m_execution.m_scaleoutCompletionWaitEvent  = WaitEvent::LTU_SIGNALING_WAIT_FOR_SCALEOUT_SEND;
            sliceState.m_execution.m_scaleoutCompletionWaitMethod = WaitMethod::GPSO_0;
        }
    }
    else
    {
        int ScaleupGroupSize = sliceState.m_dynamicComm.getScaleupGroupSize();

        bool scaleOutFirstOp =
            ((sliceState.m_currentOp == eHCLAllGather || sliceState.m_currentOp == eHCLScatter ||
              (sliceState.m_currentOp == eHCLGather && !isFirstBox) || sliceState.m_currentOp == eHCLSimpleBroadcast) &&
             ScaleupGroupSize != 1);

        if (m_scaleoutProvider->isGaudiDirect() && sliceState.m_currentOp == eHCLReduceScatter)
        {
            sliceState.m_execution.m_scaleoutCompletionWaitEvent  = WaitEvent::GDR_MEMCPY_WAIT_FOR_HNIC_RECV;
            sliceState.m_execution.m_scaleoutCompletionWaitMethod = WaitMethod::GPSO_1;
        }
        else if (sliceState.m_currentOp == eHCLReduceScatter && !isFirstBox)
        {
            if (m_scaleoutProvider->isHostNic() && !m_scaleoutProvider->isGaudiDirect())
            {
                sliceState.m_execution.m_scaleoutCompletionWaitEvent = WaitEvent::HNIC_SIGNAL_SPLIT_WAIT_FOR_PDMA;
            }
            else
            {
                bool     isEdgeIteration = sliceState.isEdgeIteration(sliceState.m_boxNumInfo);
                unsigned boxIter         = sliceState.calcBoxIterRecv(sliceState.m_boxNumInfo);
                int      longtermOffset  = isEdgeIteration ? sliceState.m_scaleoutLongtermAmount - 1
                                                           : boxIter % sliceState.m_scaleoutBuffersAmount;

                if (isEdgeIteration)
                {
                    sliceState.m_execution.m_scaleoutCompletionWaitEvent = WaitEvent::RS_SO_WAIT_FOR_ALL_RECV;
                }
                else
                {
                    sliceState.m_execution.m_scaleoutCompletionWaitEvent =
                        (WaitEvent)((unsigned)WaitEvent::RS_SO_RECV_WAIT_FOR_PREV_RECV_BASE + longtermOffset);
                }
            }

            if (sliceState.m_execution.m_scaleoutCompletionWaitEvent != WaitEvent::GENERAL_COMPLETION_EVENT)
            {
                if (sliceState.m_execution.m_scaleoutCompletionWaitEvent != WaitEvent::HNIC_SIGNAL_SPLIT_WAIT_FOR_PDMA)
                {
                    sliceState.m_execution.m_scaleoutCompletionWaitMethod = WaitMethod::GPSO_LONGTERM;
                }
                else
                {
                    sliceState.m_execution.m_scaleoutCompletionWaitMethod = WaitMethod::GPSO_1;
                }
            }
        }
        else if (sliceState.m_collectiveOp == eHCLBroadcast)
        {
            if (m_scaleoutProvider->isHostNic() && !m_scaleoutProvider->isGaudiDirect() && !sliceState.m_isSend)
            {
                sliceState.m_execution.m_scaleoutCompletionWaitEvent  = WaitEvent::HNIC_SIGNAL_SPLIT_WAIT_FOR_PDMA;
                sliceState.m_execution.m_scaleoutCompletionWaitMethod = WaitMethod::GPSO_1;
            }
            else
            {
                // This is a unique case, where two streams wait for Scale-Out Recv: the Scale-Out Send (to continue the
                // ring) and the Scale-Up Send (to do eHCLAllGather for 7 non-peer-ranks).
                sliceState.m_execution.m_scaleoutCompletionWaitEvent =
                    WaitEvent::COMPLEX_BCAST_SO_SEND_AND_AG_SU_WAIT_FOR_SO_RECV;
                sliceState.m_execution.m_scaleoutCompletionWaitMethod = WaitMethod::GPSO_LONGTERM;
            }
        }
        else if (sliceState.m_collectiveOp == eHCLSinglePeerBroadcast && sliceState.isRootPeer())
        {
            // This is a unique case, where two streams wait for Scale-Out Recv: the Scale-Out Send (to continue the
            // ring) and the Scale-Up Send (to do eHCLAllGather for 7 non-peer-ranks).
            sliceState.m_execution.m_scaleoutCompletionWaitEvent  = WaitEvent::COMPLEX_BCAST_SO_SEND_WAIT_FOR_SO_RECV;
            sliceState.m_execution.m_scaleoutCompletionWaitMethod = WaitMethod::GPSO_0;
        }
        else if (scaleOutFirstOp && !(sliceState.m_collectiveOp == eHCLReduce && sliceState.isRoot()))
        {
            sliceState.m_execution.m_scaleoutCompletionWaitEvent  = WaitEvent::SO_FIRST_SU_SEND_WAIT_FOR_SO_RECV;
            sliceState.m_execution.m_scaleoutCompletionWaitMethod = WaitMethod::GPSO_0;
        }
    }
}

void HclCollectiveRoutinesGen2Arch::provideScaleoutResources(SliceState& sliceState)
{
    if (!sliceState.isScaleoutRequired(sliceState.m_isSend, sliceState.m_boxNumInfo))
    {
        return;
    }

    hcl::ScalStream& currentStream = m_activeStreamManager.getActiveCollectiveStream(hcl::SchedulersIndex::dma);

    LOG_HCL_TRACE(HCL,
                  "need to provide {} SOBs and {} Fences",
                  sliceState.m_setup.m_scaleoutInternalSOBs,
                  sliceState.m_setup.m_scaleoutInternalFences);

    m_deviceController.setHostFences(m_streamId,
                                     currentStream.getUarchStreamIndex(),
                                     sliceState.m_isSend,
                                     sliceState.m_setup.m_scaleoutInternalFences,
                                     sliceState.m_execution.m_scaleoutFences);

    if (sliceState.m_setup.m_scaleoutInternalFences > 0)
    {
        WaitMethod waitMethod = WaitMethod::WAIT_METHOD_MAX;
        WaitEvent  waitEvent;
        if (sliceState.m_currentOp == eHCLReduceScatter && !sliceState.m_isSend)
        {
            waitMethod = WaitMethod::GPSO_1;
            if (!m_scaleoutProvider->isGaudiDirect())
            {
                m_signalsManager->enqueueWait(WaitEvent::HNIC_SCALEOUT_RECV_PDMA_WAIT_FOR_RECV,
                                              {SignalEvent::HNIC_SCALEOUT_RECV},
                                              WaitMethod::GPSO_1);
            }

            bool     isEdgeIteration = sliceState.isEdgeIteration(sliceState.m_boxNumInfo);
            unsigned boxIter         = sliceState.calcBoxIterRecv(sliceState.m_boxNumInfo);
            int      longtermOffset  = isEdgeIteration ? sliceState.m_scaleoutLongtermAmount - 1
                                                       : boxIter % sliceState.m_scaleoutBuffersAmount;
            unsigned phaseOfWait     = isEdgeIteration ? 0 : (boxIter / sliceState.m_scaleoutBuffersAmount);

            if (isEdgeIteration)
            {
                waitEvent = WaitEvent::RS_SO_WAIT_FOR_ALL_RECV;
            }
            else
            {
                waitEvent = (WaitEvent)((unsigned)WaitEvent::RS_SO_RECV_WAIT_FOR_PREV_RECV_BASE + longtermOffset);
            }
            m_signalsManager->enqueueWait(waitEvent,
                                          {SignalEvent::SIGNAL_TO_LONGTERM},
                                          WaitMethod::GPSO_LONGTERM,
                                          phaseOfWait,
                                          1,
                                          longtermOffset);
            m_signalsManager->enqueueCompletion({SignalEvent::SIGNAL_TO_CG});
        }
        else if (!m_scaleoutProvider->isGaudiDirect())
        {
            waitMethod = sliceState.m_isSend ? WaitMethod::GPSO_0 : WaitMethod::GPSO_1;
            if (!sliceState.m_isSend)
            {
                m_signalsManager->enqueueWait(WaitEvent::HNIC_SCALEOUT_RECV_PDMA_WAIT_FOR_RECV,
                                              {SignalEvent::HNIC_SCALEOUT_RECV},
                                              waitMethod);

                if (sliceState.m_currentOp == eHCLScatter)
                {
                    unsigned nextBox =
                        getNextBox(sliceState.m_dynamicComm.getMyScaleupGroup(), sliceState.m_boxIterations);
                    unsigned int numFences = (nextBox == sliceState.rootBox()) ? 1 : 2;

                    m_signalsManager->enqueueWait(WaitEvent::COMPLEX_BCAST_SO_SEND_AND_AG_SU_WAIT_FOR_SO_RECV,
                                                  {SignalEvent::SIGNAL_TO_LONGTERM},
                                                  WaitMethod::GPSO_LONGTERM,
                                                  0,
                                                  numFences);
                    m_signalsManager->enqueueCompletion({SignalEvent::SIGNAL_TO_CG});
                }
            }
        }

        if (waitMethod != WaitMethod::WAIT_METHOD_MAX)
        {
            addScaleoutInternalSOB(sliceState, waitMethod);
        }
    }

    sliceState.m_execution.m_completionSoAddr =
        m_signalsManager->dequeueSoAddress(sliceState.m_setup.m_scaleoutCompletionWaitSignal);
}

void HclCollectiveRoutinesGen2Arch::provideScaleoutResources(NonCollectiveState& nonCollectiveState)
{
    hcl::ScalStream& currentStream = m_activeStreamManager.getActiveCollectiveStream(hcl::SchedulersIndex::dma);
    LOG_HCL_TRACE(HCL,
                  "(NonCollectiveState): m_isSend={}, m_comm={}, m_isScaleoutRequired={}",
                  nonCollectiveState.m_isSend,
                  nonCollectiveState.m_comm,
                  nonCollectiveState.m_isScaleoutRequired);
    m_deviceController.setHostFences(m_streamId,
                                     currentStream.getUarchStreamIndex(),
                                     nonCollectiveState.m_isSend,
                                     nonCollectiveState.m_setup.m_scaleoutInternalFences,
                                     nonCollectiveState.m_execution.m_scaleoutFences);

    if (!nonCollectiveState.isScaleOutRequired())
    {
        return;
    }

    VERIFY(nonCollectiveState.m_setup.m_scaleoutInternalSOBs <= 1, "can only provide 1 SOB per send/recv");
    if (nonCollectiveState.m_setup.m_scaleoutInternalSOBs > 0)
    {
        WaitMethod waitMethod = nonCollectiveState.m_isSend ? WaitMethod::GPSO_0 : WaitMethod::GPSO_1;
        SobInfo    sob        = {.smIdx = m_deviceController.getSyncParams(m_streamId).m_smInfo.soSmIndex,
                                 .dcore = m_deviceController.getSyncParams(m_streamId).m_smInfo.soDcoreIndex,
                                 .ssm   = 0,
                                 .sobId = m_graphSync.getCurrentGeneralPurposeSo(waitMethod)};
        nonCollectiveState.m_execution.m_scaleoutInternalSOBs.push_back(sob);
        m_signalsManager->markMethodForCleanup(waitMethod);
    }

    nonCollectiveState.m_execution.m_completionSoAddr = nonCollectiveState.m_completionSoAddr;
}

void HclCollectiveRoutinesGen2Arch::getDeviceToRemoteIndex(CommonState&   commonState,
                                                           bool           isSend,
                                                           box_devices_t& deviceToRemoteIndex,
                                                           bool           isAllGatherQp)
{
    // initialize the output array
    deviceToRemoteIndex.fill(-1);
    unsigned currentIndex = 0;

    // Get all the scale up ranks
    const auto& innerRanks = commonState.m_dynamicComm.getInnerRanksInclusive();
    for (auto rank : innerRanks)
    {
        // get the device ID
        int      moduleID      = commonState.m_dynamicComm.m_remoteDevices[rank]->header.hwModuleID;
        unsigned isMyRank      = rank == commonState.m_dynamicComm.getMyRank();
        bool     incWqeTracker = false;

        // calc the correct offset for each device ID
        switch (commonState.m_currentOp)
        {
            case eHCLAllGather:
                if (commonState.m_collectiveOp == eHCLSinglePeerBroadcast)
                {
                    if (!commonState.isRootPeerInclusive(rank))
                    {
                        deviceToRemoteIndex[moduleID] = currentIndex++;
                        if (!isMyRank && !isSend)
                        {
                            incWqeTracker = true;
                        }
                    }
                }
                else
                {
                    deviceToRemoteIndex[moduleID] = currentIndex++;
                    if (!isMyRank && !isSend)
                    {
                        incWqeTracker = true;
                    }
                }
                break;
            case eHCLScatter:
                if (commonState.m_collectiveOp == eHCLBroadcast)
                {
                    if (commonState.isRoot())
                    {
                        deviceToRemoteIndex[moduleID] = currentIndex++;
                    }
                    else if (rank == commonState.m_root)
                    {
                        deviceToRemoteIndex[moduleID] = commonState.m_dynamicComm.getRankInScaleupGroup();
                    }

                    if (!isSend && !isMyRank && rank == commonState.m_root)
                    {
                        incWqeTracker = true;
                    }
                }
                else  // eHCLSinglePeerBroadcast
                {
                    if (commonState.isRootOrRootPeer())
                    {
                        if (isSend && !commonState.isRootPeerInclusive(rank))
                        {
                            deviceToRemoteIndex[moduleID] = currentIndex++;
                        }
                    }
                    else
                    {
                        if (!isSend && commonState.isRootPeerInclusive(rank))
                        {
                            deviceToRemoteIndex[moduleID] = commonState.m_dynamicComm.getRankInScaleupGroup();
                            if (!isMyRank)
                            {
                                incWqeTracker = true;
                            }

                            if (commonState.m_dynamicComm.getMyRank() > rank)
                            {
                                // remove the rank(-1)
                                deviceToRemoteIndex[moduleID]--;
                            }
                        }
                    }
                }

                if (m_boxType == LOOPBACK)
                {
                    deviceToRemoteIndex[moduleID] = 0;
                    if (!isMyRank && !isSend)
                    {
                        incWqeTracker = true;
                    }
                }
                break;
            case eHCLSimpleBroadcast:
                if (m_boxType == LOOPBACK || isSend || rank == commonState.m_root ||
                    commonState.m_dynamicComm.arePeers(rank, commonState.m_root))
                {
                    deviceToRemoteIndex[moduleID] = 0;
                    if (!isMyRank && !isSend)
                    {
                        incWqeTracker = true;
                    }
                }
                break;
            case eHCLGather:
                if (commonState.isRoot())
                {
                    deviceToRemoteIndex[moduleID] = currentIndex;
                    ++currentIndex;
                    if (!isMyRank && !isSend)
                    {
                        incWqeTracker = true;
                    }
                }
                else if (rank == commonState.m_root)
                {
                    deviceToRemoteIndex[moduleID] = commonState.m_dynamicComm.getRankInScaleupGroup();
                }
                break;
            case eHCLAllReduce:
            case eHCLReduceScatter:
            case eHCLAll2All:
            case eHCLNoCollective:
                deviceToRemoteIndex[moduleID] = currentIndex;
                ++currentIndex;
                if (!isMyRank && !isSend)
                {
                    incWqeTracker = true;
                }
                break;
            case eHCLBroadcast:
            case eHCLSinglePeerBroadcast:
            case eHCLReduce:
            case eHCLCollectiveLastValue:
                VERIFY(false, "getDeviceToRemoteIndex: unsupported current op {}", commonState.m_currentOp);
        }

        if (incWqeTracker && !m_nullSubmit)
        {
            m_wqeTracker->incWqe(commonState.m_dynamicComm,
                                 mod(rank, commonState.m_dynamicComm.getScaleupGroupSize()),
                                 isAllGatherQp ? QpType::ScaleUpAllGather : QpType::ScaleUpReduceScatter);
        }
    }
}

unsigned int HclCollectiveRoutinesGen2Arch::calcRequiredCreditAmount(CommonState&     commonState,
                                                                     BoxNumInfo&      nexBoxNumInfo,
                                                                     BoxNumInfo&      prevBoxNumInfo,
                                                                     unsigned         sliceIter,
                                                                     unsigned         boxIter,
                                                                     const unsigned   firstBoxIter,
                                                                     bool             isFirstOp,
                                                                     HCL_CollectiveOp currentOp,
                                                                     int64_t          dependencyTargetVal)
{
    const int64_t cgSize               = m_graphSync.getCgData(false).size;
    hcl_flags_t   flags                = commonState.m_userFlags;
    unsigned      requiredExtraCredits = 0;
    const bool    firstIter            = (sliceIter == 0 && boxIter == firstBoxIter && isFirstOp);

    if (!getGroupContext() && dependencyTargetVal != 0)
    {
        LOG_HCL_DEBUG(HCL, "Dependency on previous operation - dependencyTargetVal={}", dependencyTargetVal);
        int64_t signalDiff = m_longSo.targetValue - dependencyTargetVal;
        if (signalDiff != 0)
        {
            requiredExtraCredits =
                std::max((unsigned)((signalDiff < cgSize) ? (cgSize - signalDiff) : 0), (unsigned)requiredExtraCredits);
        }
    }

    if (m_staticBuffersAllocator.isValid())
    {
        requiredExtraCredits = m_staticBuffersAllocator.alloc(m_intermediateBufferManager,
                                                              m_longSo,
                                                              cgSize,
                                                              requiredExtraCredits,
                                                              m_graphSync.getLtuData());
    }

    if (m_scaleoutProvider->isGaudiDirect() && boxIter > 0 && commonState.m_currentOp == eHCLReduceScatter)
    {
        unsigned continuousTargets = 0;
        if (commonState.m_collectiveOp != eHCLReduce)
        {
            continuousTargets =
                std::min(commonState.m_scaleoutBuffersAmount, commonState.m_boxIterations - boxIter - 1);
            if (commonState.isEdgeIteration(prevBoxNumInfo) && commonState.m_collectiveOp != eHCLReduceScatter)
            {
                continuousTargets += 1;
            }
        }

        uint64_t lastTargetVal =
            m_intermediateBufferManager.allocNextBuffer(m_longSo.targetValue + continuousTargets, SCALEOUT_GDR_POOL);

        LOG_HCL_TRACE(HCL_ECR,
                      "IMB allocation: SCALEOUT GDR pool, current SO {}, free at {}",
                      m_longSo.targetValue,
                      m_longSo.targetValue + continuousTargets);

        if (lastTargetVal != 0)
        {
            VERIFY(m_longSo.targetValue > lastTargetVal, "No available intermediate buffer");

            const int64_t signalDiff = m_longSo.targetValue - lastTargetVal;
            requiredExtraCredits =
                std::max((unsigned)((signalDiff < cgSize) ? (cgSize - signalDiff) : 0), (unsigned)requiredExtraCredits);
        }
    }

    if (m_scaleoutProvider->isHostNic() && !m_scaleoutProvider->isGaudiDirect())
    {
        // handle send/recv hnics scaleout send
        if (commonState.m_scaleoutNonCollectiveSend > 0)
        {
            LOG_HCL_TRACE(HCL,
                          "hnics & scaleout send, m_scaleoutNonCollectiveSend={}",
                          commonState.m_scaleoutNonCollectiveSend);
            const uint64_t lastTargetVal = m_scaleoutProvider->getHostBufferManager(m_streamId)
                                               ->allocNextBuffer(m_longSo.targetValue, HNIC_SEND_POOL);
            if (lastTargetVal != 0)
            {
                VERIFY(m_longSo.targetValue > lastTargetVal, "No available send host buffer");
                const int64_t signalDiff = m_longSo.targetValue - lastTargetVal;
                requiredExtraCredits     = std::max((unsigned)((signalDiff < cgSize) ? (cgSize - signalDiff) : 0),
                                                (unsigned)requiredExtraCredits);
            }
        }
        else if (commonState.isScaleoutRequired(true, nexBoxNumInfo) ||
                 m_signalsManager->isEventRegistered(SignalEvent::HNIC_SCALEOUT_SEND))
        {
            uint64_t lastTargetVal = m_scaleoutProvider->getHostBufferManager(m_streamId)
                                         ->allocNextBuffer(m_longSo.targetValue, HNIC_SEND_POOL);
            if (lastTargetVal != 0)
            {
                VERIFY(m_longSo.targetValue > lastTargetVal, "No available send host buffer");

                int64_t signalDiff   = m_longSo.targetValue - lastTargetVal;
                requiredExtraCredits = std::max((unsigned)((signalDiff < cgSize) ? (cgSize - signalDiff) : 0),
                                                (unsigned)requiredExtraCredits);
            }
        }

        // handle send/recv hnics scaleout send
        if (commonState.m_scaleoutNonCollectiveRecv > 0)
        {
            LOG_HCL_TRACE(HCL,
                          "hnics & scaleout recv, m_scaleoutNonCollectiveRecv={}",
                          commonState.m_scaleoutNonCollectiveRecv);
            const uint64_t lastTargetVal = m_scaleoutProvider->getHostBufferManager(m_streamId)
                                               ->allocNextBuffer(m_longSo.targetValue, HNIC_RECV_POOL);
            if (lastTargetVal != 0)
            {
                VERIFY(m_longSo.targetValue > lastTargetVal, "No available recv host buffer");
                const int64_t signalDiff = m_longSo.targetValue - lastTargetVal;
                requiredExtraCredits     = std::max((unsigned)((signalDiff < cgSize) ? (cgSize - signalDiff) : 0),
                                                (unsigned)requiredExtraCredits);
            }
        }
        else if (commonState.isScaleoutRequired(false, prevBoxNumInfo) ||
                 m_signalsManager->isEventRegistered(SignalEvent::HNIC_SCALEOUT_RECV))
        {
            unsigned continuousTargets = 0;
            if (commonState.m_currentOp == eHCLReduceScatter)
            {
                continuousTargets =
                    std::min(commonState.m_scaleoutBuffersAmount, commonState.m_boxIterations - boxIter - 1);
                if (commonState.isEdgeIteration(prevBoxNumInfo) && commonState.m_collectiveOp != eHCLReduceScatter)
                {
                    continuousTargets += 1;
                }
                LOG_HCL_TRACE(HCL, "allocating hnic recv buffer for {} future collectives", continuousTargets);
            }
            uint64_t lastTargetVal = m_scaleoutProvider->getHostBufferManager(m_streamId)
                                         ->allocNextBuffer(m_longSo.targetValue + continuousTargets, HNIC_RECV_POOL);
            if (lastTargetVal != 0)
            {
                VERIFY(m_longSo.targetValue > lastTargetVal, "No available recv host buffer");

                int64_t signalDiff   = m_longSo.targetValue - lastTargetVal;
                requiredExtraCredits = std::max((unsigned)((signalDiff < cgSize) ? (cgSize - signalDiff) : 0),
                                                (unsigned)requiredExtraCredits);
            }
        }
    }

    uint64_t lastTargetVal =
        m_deviceController.getSyncParams(m_streamId).m_regularGPSOManager->allocNextCredit(m_longSo.targetValue);
    if (lastTargetVal != 0)
    {
        VERIFY(m_longSo.targetValue > lastTargetVal, "No available regular gpso");
        int64_t signalDiff = m_longSo.targetValue - lastTargetVal;
        requiredExtraCredits =
            std::max((unsigned)((signalDiff < cgSize) ? (cgSize - signalDiff) : 0), (unsigned)requiredExtraCredits);
    }

    if (commonState.isLongtermGPSORequired(boxIter))
    {
        uint64_t lastTargetValLongTerm = 0;
        int64_t  signalDiff            = 0;
        unsigned continuousTarget      = commonState.calcLongtermContinuousTarget(boxIter);

        LOG_HCL_TRACE(HCL, "Allocating longterm gpso for continuousTarget={} future collectives", continuousTarget);
        m_graphSync.incLongtermSoIndex(commonState.m_scaleoutLongtermAmount);

        for (unsigned i = 0; i < commonState.m_scaleoutLongtermAmount; ++i)
        {
            lastTargetValLongTerm =
                m_deviceController.getSyncParams(m_streamId)
                    .m_longtermGPSOManager->allocNextCredit(m_longSo.targetValue + continuousTarget);
            if (lastTargetValLongTerm != 0)
            {
                VERIFY(m_longSo.targetValue > lastTargetValLongTerm, "No available longterm gpso");

                signalDiff           = m_longSo.targetValue - lastTargetValLongTerm;
                requiredExtraCredits = std::max((unsigned)((signalDiff < cgSize) ? (cgSize - signalDiff) : 0),
                                                (unsigned)requiredExtraCredits);
            }
        }
    }

    bool           firstOpInGroup      = false;
    const uint64_t groupMaxTargetValue = getGroupMaxTargetValue();
    if (getGroupContext())
    {
        if (m_groupContextStrongOrder)
        {
            flags.weak_order = true;
        }
        else
        {
            m_groupContextStrongOrder = true;
            firstOpInGroup            = true;
            if (GCFG_ENABLE_DEPENDENCY_CHECKER.value())
            {
                LOG_HCL_TRACE(HCL, "Waiting on target value {}", groupMaxTargetValue);
                m_dependencyChecker->updateDb(groupMaxTargetValue);
                setGroupMaxTargetValue(0);
            }
        }
    }

    if (!GCFG_WEAK_ORDER.isSetFromDefault())
    {
        flags.weak_order = GCFG_WEAK_ORDER.value();
        LOG_HCL_DEBUG(HCL, "weak order flag = {}", flags.weak_order);
    }

    if (0 == flags.weak_order && firstIter)
    {
        if (GCFG_ENABLE_DEPENDENCY_CHECKER.value())
        {
            if (firstOpInGroup && groupMaxTargetValue)
            {
                int64_t signalDiff   = m_longSo.targetValue - groupMaxTargetValue;
                requiredExtraCredits = std::max((unsigned)((signalDiff < cgSize) ? (cgSize - signalDiff) : 0),
                                                (unsigned)requiredExtraCredits);
            }
        }
        else
        {
            LOG_HCL_TRACE(HCL, "applying strong order in beginning of operation");
            // Strong Order
            requiredExtraCredits = cgSize - 1;
        }
    }

    if (unlikely(m_requestStrongOrderIter))
    {
        requiredExtraCredits     = cgSize - 1;
        m_requestStrongOrderIter = false;
    }

    requiredExtraCredits = std::max((unsigned)1, requiredExtraCredits);

    return m_deviceController.handleExtraCredits(m_streamId, requiredExtraCredits);
}

void HclCollectiveRoutinesGen2Arch::addScaleoutInternalSOB(SliceState& sliceState, WaitMethod method)
{
    SobInfo sob = {.smIdx = m_deviceController.getSyncParams(m_streamId).m_smInfo.soSmIndex,
                   .dcore = m_deviceController.getSyncParams(m_streamId).m_smInfo.soDcoreIndex,
                   .ssm   = 0,
                   .sobId = m_graphSync.getCurrentGeneralPurposeSo(method)};
    sliceState.m_execution.m_scaleoutInternalSOBs.push_back(sob);
    if (method != WaitMethod::GPSO_LONGTERM)
    {
        m_signalsManager->markMethodForCleanup(method);
    }
}
