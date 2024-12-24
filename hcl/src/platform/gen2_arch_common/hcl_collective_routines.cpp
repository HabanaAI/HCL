#include "platform/gen2_arch_common/hcl_collective_routines.h"

#include <ext/alloc_traits.h>  // for __alloc...
#include <algorithm>           // for max
#include <cstdint>             // for uint64_t
#include <string>              // for string
#include <unordered_map>       // for unordered_map
#include <set>                 // for set

#include "hcl_collective_params.h"                            // for HclColl...
#include "hcl_global_conf.h"                                  // for GCFG_WE...
#include "interfaces/hcl_remote_device.h"                     // for HclRemo...
#include "interfaces/hcl_unique_sorted_vector.h"              // for UniqueS...
#include "hcl_log_manager.h"                                  // for LOG_*
#include "platform/gen2_arch_common/commands/hcl_commands.h"  // for HclComm...
#include "platform/gen2_arch_common/hcl_device.h"             // for HclDevi...
#include "platform/gen2_arch_common/collective_states.h"
#include "hcl_utils.h"  // for VERIFY
#include "platform/gen2_arch_common/device_buffer_manager.h"
#include "platform/gen2_arch_common/scaleout_provider.h"  // for ScaleoutProvider
#include "platform/gen2_arch_common/signals/calculator.h"
#include "platform/gen2_arch_common/signals/manager.h"
#include "intermediate_buffer_container.h"
#include "platform/gen2_arch_common/descriptors.h"
#include "platform/gen2_arch_common/host_buffer_manager.h"  // for HostBufferManager
#include "platform/gen2_arch_common/hcl_graph_sync.h"       // for HclGraphSyncGen2Arch
#include "platform/gen2_arch_common/wqe_tracker.h"          // for QpType, WqeTracker
#include "platform/gen2_arch_common/signals/manager.h"      // for SignalsManager
#include "platform/gen2_arch_common/dependency_checker.h"   // for DependencyChecker
#include "platform/gen2_arch_common/collective_utils.h"     // for getNextBox, getPrevBox
#include "platform/gen2_arch_common/active_stream_manager.h"
#include "platform/gen2_arch_common/hcl_device_controller.h"
#include "platform/gen2_arch_common/server_def.h"  // for Gen2ArchServerDef

#include "hcl_device_control_factory.h"
#include "hcl_math_utils.h"
#include "platform/gen2_arch_common/send_recv_aggregator.h"  // for SendRecvEntry
#include "hcl_types.h"                                       // for HCL_HwModuleId
#include "collective_interface/prims/scaleup_prims.h"
#include "collective_interface/prims/simple_prims.h"

HclCollectiveRoutinesGen2Arch::HclCollectiveRoutinesGen2Arch(HclDeviceGen2Arch* device,
                                                             int                streamId,
                                                             WqeTracker*        wqeTracker)
: m_deviceController(HclControlDeviceFactory::getDeviceControl()),
  m_device(device),
  m_streamId(streamId),
  m_graphSync(m_deviceController.getGraphSync(streamId)),
  m_boxType((HclConfigType)GCFG_BOX_TYPE_ID.value()),
  m_intermediateBufferManager(m_device->getSIB(streamId)),
  m_commands(m_deviceController.getGen2ArchCommands()),
  m_scaleoutProvider(device->getScaleOutProvider()),
  m_activeStreamManager(m_scaleoutProvider, m_deviceController, m_streamId, m_longSo),
  m_wqeTracker(wqeTracker),
  m_serverConnectivity(device->getServerConnectivity())
{
    m_nullSubmit = GCFG_HCL_NULL_SUBMIT.value();
}

HclCollectiveRoutinesGen2Arch::~HclCollectiveRoutinesGen2Arch()
{
    if (m_wqeTracker != nullptr)
    {
        delete m_wqeTracker;
    }

    if (m_signalsManager != nullptr)
    {
        delete m_signalsManager;
    }
}

/**
 * @brief initialize data related per new communicator should be called on hcclCommInitRank
 *
 * @param commId - the new communicator ID
 * @param nRanks - new communicator size
 */
void HclCollectiveRoutinesGen2Arch::onCommInit(const HCL_Comm commId)
{
    m_wqeTracker->resizeDB(commId);
}

int HclCollectiveRoutinesGen2Arch::getRemoteRankToRsi(CommonState& commonState,
                                                      bool         isSend,
                                                      HCL_Rank     remoteRank,
                                                      bool         isAllGatherQp)
{
    bool isMyRank = remoteRank == commonState.m_dynamicComm.getMyRank();
    // calc the correct offset for each remote rank
    switch (commonState.m_collectiveOp)
    {
        case eHCLSimpleBroadcast:
            if (isSend)
            {
                return 0;
            }
            else if (m_boxType == LOOPBACK || remoteRank == commonState.m_root)
            {
                m_wqeTracker->incWqe(
                    commonState.m_dynamicComm,
                    div((uint32_t)remoteRank, (uint32_t)commonState.m_dynamicComm.getScaleupGroupSize()),
                    isAllGatherQp ? QpType::ScaleOutAllGather : QpType::ScaleOutReduceScatter);
                return 0;
            }
            break;
        default:
            if (!isMyRank && !isSend)
            {
                m_wqeTracker->incWqe(
                    commonState.m_dynamicComm,
                    div((uint32_t)remoteRank, (uint32_t)commonState.m_dynamicComm.getScaleupGroupSize()),
                    isAllGatherQp ? QpType::ScaleOutAllGather : QpType::ScaleOutReduceScatter);
            }
            return commonState.m_dynamicComm.getRankToScaleupGroupMap()[remoteRank];
            break;
    }

    return -1;
}

void HclCollectiveRoutinesGen2Arch::barrierArmSchedulers(unsigned requiredCredits, HCL_CollectiveOp currentOp)
{
    // Barrier Arm all Schedulers except DMA (which will come later)
    for (unsigned schedIdx = (unsigned)hcl::SchedulersIndex::sendScaleUp;
         schedIdx < (unsigned)hcl::SchedulersIndex::count;
         schedIdx++)
    {
        hcl::ScalStream& currentStream =
            m_activeStreamManager.getActiveCollectiveStream((hcl::SchedulersIndex)schedIdx);
        currentStream.setTargetValue(m_longSo.targetValue);

        hcl::ScalStream& arbitratorStream =
            m_deviceController.getScalStream(m_streamId, currentStream.getSchedIdx(), ARB_STREAM_IDX);
        arbitratorStream.setTargetValue(m_longSo.targetValue);

        m_deviceController.addBarrierArm(arbitratorStream,
                                         false,
                                         requiredCredits,
                                         {currentStream.getUarchStreamIndex()});
        m_deviceController.waitForBarrierArm(currentStream);
    }

    // DMA Scheduler Barrier Arm
    hcl::ScalStream* arbitratorStream = m_activeStreamManager.getDmaScalStream(hcl::DMAStreams::arbitrator);
    m_deviceController.addBarrierArm(*arbitratorStream,
                                     false,
                                     requiredCredits,
                                     m_activeStreamManager.getActiveDmaStreams());

    for (unsigned streamId : m_activeStreamManager.getActiveDmaStreams())
    {
        hcl::ScalStream* currentStream = m_activeStreamManager.getDmaScalStream((hcl::DMAStreams)streamId);
        currentStream->setTargetValue(m_longSo.targetValue);

        m_deviceController.waitForBarrierArm(*currentStream);
    }
}

void HclCollectiveRoutinesGen2Arch::configureExternalSoForCompletion(unsigned completionSignals)
{
    hcl::ScalStream& currentStream = m_activeStreamManager.getActiveCollectiveStream(hcl::SchedulersIndex::sendScaleUp);
    m_commands.serializeLbwWriteCommand(
        currentStream,
        currentStream.getSchedIdx(),
        m_signalsManager->getSoAddress(WaitMethod::EXTERNAL_CG_SO),
        m_graphSync.getSoConfigValue(getScalUtils()->getCMaxTargetValue() - completionSignals, true));
}

void HclCollectiveRoutinesGen2Arch::streamAddSingleWaitIfNeeded(hcl::ScalStream&                           scalStream,
                                                                llvm_vecsmall::SmallVector<WaitEvent, 8>&& waitEvents)
{
    for (WaitEvent waitEvent : waitEvents)
    {
        SyncObjectDescriptor desc = m_signalsManager->getSobDesc(waitEvent);
        if (desc.value > 0)
        {
            LOG_HCL_DEBUG(HCL,
                          "Adding fence (schedIdx {}, stream {}) for event {} ({}, value {})",
                          scalStream.getSchedIdx(),
                          scalStream.getUarchStreamIndex(),
                          waitEvent,
                          m_utils->printSOBInfo(desc.sob),
                          desc.value);
            m_deviceController.streamAddWait(scalStream, desc);
            m_signalsManager->finalize(waitEvent);
            break;
        }
    }
}

void HclCollectiveRoutinesGen2Arch::syncWithLtuIfNeeded(SliceState& sliceState, hcl::ScalStream& scalStream)
{
    unsigned scaleupBufferIdx = m_intermediateBufferManager.getCurrentBufferIdx(SCALEUP_AND_ALL2ALL_POOL);
    if (sliceState.m_syncUpBufferWithLtu && m_graphSync.getLtuData()[scaleupBufferIdx].first)
    {
        SobInfo              sobInfo = m_utils->getSOBInfo(m_graphSync.getCurrentLtuGpsoAddr(scaleupBufferIdx));
        unsigned             soVal   = m_graphSync.getCurrentLtuGpsoData(scaleupBufferIdx);
        SyncObjectDescriptor sobDesc = {.sob = sobInfo, .value = soVal};
        m_deviceController.streamAddWait(scalStream, sobDesc, true);
    }
}

uint32_t HclCollectiveRoutinesGen2Arch::getSoConfigValue(unsigned value, bool isReduction)
{
    return m_graphSync.getSoConfigValue(value, isReduction);
}

WqeWraparoundBits HclCollectiveRoutinesGen2Arch::getWraparoundBits(HCL_Comm commId, unsigned rank, QpType qpType)
{
    return m_wqeTracker->getWqeWraparoundBits(commId, rank, qpType);
}

void HclCollectiveRoutinesGen2Arch::setGroupContext(bool value)
{
    m_groupContext = value;
    if (value == false)
    {
        m_groupContextStrongOrder = false;
    }
}

hcclResult_t HclCollectiveRoutinesGen2Arch::sendRecv(hcl::GroupCallsBuckets&            groupCallsBuckets,
                                                     std::vector<SendRecvMemCopyEntry>& sendRecvMemCpyVec,
                                                     HCL_Comm                           comm,
                                                     const std::set<HCL_Rank>&          remoteRanks,
                                                     uint8_t                            apiId)
{
    ScopedNullSubmit scopedNullSubmit(m_streamId, m_deviceController);

    const bool isHnicsRequired = m_scaleoutProvider->isHostNic();
    LOG_HCL_TRACE(HCL,
                  "comm={}, sendRecvMemCpyVec.size={}, isHnicsRequired={}",
                  comm,
                  sendRecvMemCpyVec.size(),
                  isHnicsRequired);
    std::lock_guard<std::mutex> lock(m_deviceController.getStreamLock(m_streamId));

    std::set<HCL_Rank> remoteOuterRanks;
    for (const HCL_Rank remoteRank : remoteRanks)
    {
        if (!m_device->getComm(comm).isRankInsideScaleupGroup(remoteRank))
        {
            remoteOuterRanks.insert(remoteRank);
        }
    }

    if (unlikely(LOG_LEVEL_AT_LEAST_TRACE(HCL)))
    {
        const ranks_vector remoteOuterRanksVec(remoteOuterRanks.begin(), remoteOuterRanks.end());
        UniqueSortedVector remoteOuterRanksVecSorted;
        remoteOuterRanksVecSorted.insert_range_sorted(remoteOuterRanksVec.begin(), remoteOuterRanksVec.end());
        LOG_HCL_TRACE(HCL, "comm={}, remoteOuterRanksVecSorted={}", comm, remoteOuterRanksVecSorted);
    }
    m_device->openAllRequiredNonPeerQPs(comm, remoteOuterRanks);

    const auto& scaleupSendGroups = groupCallsBuckets[hcl::SchedulersIndex::sendScaleUp].getGroupCalls();
    const auto& scaleupRecvGroups = groupCallsBuckets[hcl::SchedulersIndex::recvScaleUp].getGroupCalls();
    LOG_HCL_TRACE(HCL,
                  "scaleupSendGroups.size={}, scaleupRecvGroups.size={}",
                  scaleupSendGroups.size(),
                  scaleupRecvGroups.size());
    LOG_HCL_TRACE(HCL, "scaleupSendGroups={}", scaleupSendGroups);
    LOG_HCL_TRACE(HCL, "scaleupRecvGroups={}", scaleupRecvGroups);

    auto pred    = [](const SendRecvEntry& entry) { return entry.isValid; };
    auto calcMax = [this, &pred](const hcl::GroupCallsAggregation& groupCalls) {
        unsigned maxNumber = 0;
        for (auto vec : groupCalls)
        {
            if (std::none_of(vec.second.begin(), vec.second.end(), pred))
            {
                continue;
            }
            LOG_HCL_TRACE(HCL,
                          "calcMax: maxNumber={}, vec.key={}, vec.size={}",
                          maxNumber,
                          vec.first,
                          vec.second.size());
            maxNumber = std::max(maxNumber, (unsigned)vec.second.size());
        }
        return maxNumber;
    };

    const unsigned maxNumberOfSend = calcMax(scaleupSendGroups);
    const unsigned maxNumberOfRecv = calcMax(scaleupRecvGroups);
    LOG_HCL_TRACE(HCL, "maxNumberOfSend={}, maxNumberOfRecv={}", maxNumberOfSend, maxNumberOfRecv);

    auto&       scaleoutSendBucket = groupCallsBuckets[hcl::SchedulersIndex::sendScaleOut];
    auto&       scaleoutRecvBucket = groupCallsBuckets[hcl::SchedulersIndex::recvScaleOut];
    const auto& scaleoutSendGroups = scaleoutSendBucket.getGroupCalls();
    const auto& scaleoutRecvGroups = scaleoutRecvBucket.getGroupCalls();
    LOG_HCL_TRACE(HCL,
                  "scaleoutSendGroups.size={}, scaleoutRecvGroups.size={}",
                  scaleoutSendGroups.size(),
                  scaleoutRecvGroups.size());
    LOG_HCL_TRACE(HCL, "scaleoutSendGroups={}", scaleoutSendGroups);
    LOG_HCL_TRACE(HCL, "scaleoutRecvGroups={}", scaleoutRecvGroups);
    const HCL_Rank myRank           = m_device->getMyRank(comm);
    const unsigned myBox            = m_device->getComm(comm).getRankToScaleupGroupMap()[myRank];
    const HCL_Rank lastRank         = m_device->getComm(comm).getScaleOutLastRank();
    const unsigned lastBox          = m_device->getComm(comm).getRankToScaleupGroupMap()[lastRank];
    const unsigned ScaleupGroupSize = m_device->getComm(comm).getScaleupGroupSize();
    const unsigned numOfCommRanks   = ScaleupGroupSize * (lastBox + 1);
    BoxNumInfo     myBoxNumInfo(myBox, BoxNumInfo::boxOrientation::MY_BOX);
    LOG_HCL_TRACE(HCL,
                  "myRank={}, myBox{}, lastRank={}, lastBox={}, ScaleupGroupSize={}, numOfCommRanks={}",
                  myRank,
                  myBox,
                  lastRank,
                  lastBox,
                  ScaleupGroupSize,
                  numOfCommRanks);

    const SendRecvVector& orderedSendList =
        scaleoutSendBucket.buildIterationsLayout(true, myRank, myBox, lastBox + 1, numOfCommRanks);
    LOG_HCL_TRACE(HCL, "orderedSendList.size={}, orderedSendList={}", orderedSendList.size(), orderedSendList);
    const SendRecvVector& orderedRecvList =
        scaleoutRecvBucket.buildIterationsLayout(false, myRank, myBox, lastBox + 1, numOfCommRanks);
    LOG_HCL_TRACE(HCL, "orderedRecvList.size={}, orderedRecvList={}", orderedRecvList.size(), orderedRecvList);

    const unsigned maxNumberOfScaleoutSend = orderedSendList.size();
    const unsigned maxNumberOfScaleoutRecv = orderedRecvList.size();
    LOG_HCL_TRACE(HCL,
                  "maxNumberOfScaleoutSend={}, maxNumberOfScaleoutRecv={}",
                  maxNumberOfScaleoutSend,
                  maxNumberOfScaleoutRecv);

    VERIFY(maxNumberOfSend + maxNumberOfRecv + sendRecvMemCpyVec.size() + maxNumberOfScaleoutSend +
                   maxNumberOfScaleoutRecv >
               0,
           "No sends or recvs provided!");

    // The nics can't give back pressure towards the engine-arc, so it's the SW responsibility to prevent overflow in
    // the wqe table.
    // The idea is to make sure that there are no more than one send nor more than one recv per QP per CG credit.
    // If the user asked that (between group start and group end) the below loop will split them to signal to
    // different credits.
    const unsigned numIterations = std::max(std::max(std::max(maxNumberOfSend, maxNumberOfRecv),
                                                     std::max(maxNumberOfScaleoutSend, maxNumberOfScaleoutRecv)),
                                            (unsigned)sendRecvMemCpyVec.size());
    LOG_HCL_TRACE(HCL, "numIterations={}", numIterations);

    std::unordered_map<HCL_Rank, unsigned> qpSetIterPerSendPeerRank;
    for (const SendRecvEntry& entry : orderedSendList)
    {
        if (m_device->getComm(comm).isPeer(entry.remoteRank))
        {
            qpSetIterPerSendPeerRank[entry.remoteRank] = 0;
            LOG_HCL_TRACE(HCL, "Added qpSetIterPerSendPeerRank for rank ", entry.remoteRank);
        }
    }

    std::unordered_map<HCL_Rank, unsigned> qpSetIterPerRecvPeerRank;
    for (const SendRecvEntry& entry : orderedRecvList)
    {
        if (m_device->getComm(comm).isPeer(entry.remoteRank))
        {
            qpSetIterPerRecvPeerRank[entry.remoteRank] = 0;
            LOG_HCL_TRACE(HCL, "Added qpSetIterPerRecvPeerRank for rank ", entry.remoteRank);
        }
    }
    LOG_HCL_TRACE(HCL,
                  "qpSetIterPerSendPeerRank.size={}, qpSetIterPerRecvPeerRank.size()={}",
                  qpSetIterPerSendPeerRank.size(),
                  qpSetIterPerRecvPeerRank.size());

    uint64_t startTgtVal = m_longSo.targetValue;

    const QpType srStream = QpType::ScaleUpReduceScatter;

    const DevicesSet& hwModules = m_device->getServerDef().getHwModules();
    LOG_HCL_TRACE(HCL, "hwModules=[ {} ]", hwModules);

    for (unsigned iter = 0; iter < numIterations; ++iter)
    {
        // New Api call -> inc the long SO
        m_deviceController.advanceProg(m_streamId, false);
        LOG_HCL_CONTEXT_INFO(HCL,
                             "Running another sendRecv iteration, iter={} targetValue={}",
                             iter,
                             m_longSo.targetValue);

        // this assumes that all inner vectors have the same size and
        // allocates space for the complete result in advance
        SendRecvArray scaleupSendIter = {};
        SendRecvArray scaleupRecvIter = {};

        uint64_t dependencyTargetVal        = 0;
        uint64_t dependencyRunningTargetVal = 0;

        uint32_t sendCnt = 0, recvCnt = 0;

        for (const HCL_HwModuleId hwModId : hwModules)
        {
            if (scaleupSendGroups.count(hwModId) && (iter < scaleupSendGroups.at(hwModId).size()))
            {
                scaleupSendIter[hwModId] = scaleupSendGroups.at(hwModId)[iter];
                sendCnt++;

                if (!GCFG_WEAK_ORDER.value() && GCFG_ENABLE_DEPENDENCY_CHECKER.value())
                {
                    dependencyRunningTargetVal = checkSendRecvDependency(
                        scaleupSendIter[hwModId].address,
                        scaleupSendIter[hwModId].count * dataTypeSizeInBytes(scaleupSendIter[hwModId].dataType),
                        m_longSo.targetValue,
                        true);
                    dependencyTargetVal = std::max(dependencyTargetVal, dependencyRunningTargetVal);
                }
            }

            if (scaleupRecvGroups.count(hwModId) && (iter < scaleupRecvGroups.at(hwModId).size()))
            {
                scaleupRecvIter[hwModId] = scaleupRecvGroups.at(hwModId)[iter];

                if (!GCFG_WEAK_ORDER.value() && GCFG_ENABLE_DEPENDENCY_CHECKER.value())
                {
                    dependencyRunningTargetVal = checkSendRecvDependency(
                        scaleupRecvIter[hwModId].address,
                        scaleupRecvIter[hwModId].count * dataTypeSizeInBytes(scaleupRecvIter[hwModId].dataType),
                        m_longSo.targetValue,
                        false);
                    dependencyTargetVal = std::max(dependencyTargetVal, dependencyRunningTargetVal);
                }

                recvCnt++;
                m_wqeTracker->incWqe(comm, mod(hwModId, ScaleupGroupSize), srStream);
            }
        }

        LOG_HCL_TRACE(HCL, "iter={}, scaleupSendIter=[ {} ]", iter, scaleupSendIter);
        LOG_HCL_TRACE(HCL, "iter={}, scaleupRecvIter=[ {} ]", iter, scaleupRecvIter);

        const SendRecvVector scaleoutSendIter = scaleoutSendBucket.createScaleoutIterationEntries(iter);
        const SendRecvVector scaleoutRecvIter = scaleoutRecvBucket.createScaleoutIterationEntries(iter);
        LOG_HCL_TRACE(HCL, "iter={}, scaleoutSendIter={}", iter, scaleoutSendIter);
        LOG_HCL_TRACE(HCL, "iter={}, scaleoutRecvIter={}", iter, scaleoutRecvIter);

        const unsigned iterScaleoutSignals =
            countScaleOutSignalsSendRecv(scaleoutSendIter.size(), scaleoutRecvIter.size(), comm);
        LOG_HCL_TRACE(HCL, "iter={}, iterScaleoutSignals={}", iter, iterScaleoutSignals);

        if (!GCFG_WEAK_ORDER.value() && GCFG_ENABLE_DEPENDENCY_CHECKER.value())
        {
            for (auto& scaleOutSend : scaleoutSendIter)
            {
                dependencyRunningTargetVal =
                    checkSendRecvDependency(scaleOutSend.address,
                                            scaleOutSend.count * dataTypeSizeInBytes(scaleOutSend.dataType),
                                            m_longSo.targetValue,
                                            true);
                dependencyTargetVal = std::max(dependencyTargetVal, dependencyRunningTargetVal);
            }

            for (auto& scaleOutRecv : scaleoutRecvIter)
            {
                dependencyRunningTargetVal =
                    checkSendRecvDependency(scaleOutRecv.address,
                                            scaleOutRecv.count * dataTypeSizeInBytes(scaleOutRecv.dataType),
                                            m_longSo.targetValue,
                                            false);
                dependencyTargetVal = std::max(dependencyTargetVal, dependencyRunningTargetVal);
            }
        }

        // The next calls are to fill the five sched with programs

        // Determine next self rank send/recv DMA
        std::vector<SendRecvMemCopyEntry> iterMemcpyVec;  // can be empty vector if no self s/r in current iter
        if (iter < sendRecvMemCpyVec.size())
        {
            iterMemcpyVec.push_back(sendRecvMemCpyVec.at(iter));

            if (!GCFG_WEAK_ORDER.value() && GCFG_ENABLE_DEPENDENCY_CHECKER.value())
            {
                // Src Address
                dependencyRunningTargetVal = checkSendRecvDependency(iterMemcpyVec[0].sendBaseAddress,
                                                                     iterMemcpyVec[0].chunkCount *
                                                                         dataTypeSizeInBytes(iterMemcpyVec[0].dataType),
                                                                     m_longSo.targetValue,
                                                                     true);
                dependencyTargetVal        = std::max(dependencyTargetVal, dependencyRunningTargetVal);

                // Dest Address
                dependencyRunningTargetVal = checkSendRecvDependency(iterMemcpyVec[0].recvBaseAddress,
                                                                     iterMemcpyVec[0].chunkCount *
                                                                         dataTypeSizeInBytes(iterMemcpyVec[0].dataType),
                                                                     m_longSo.targetValue,
                                                                     false);
                dependencyTargetVal        = std::max(dependencyTargetVal, dependencyRunningTargetVal);
            }
        }

        // check if any scaleup ranks send/recv in this iteration
        unsigned currentNumberOfSend = maxNumberOfSend > iter ? 1 : 0;
        unsigned currentNumberOfRecv = maxNumberOfRecv > iter ? 1 : 0;
        if (currentNumberOfSend == 0 && currentNumberOfRecv == 0 && ScaleupGroupSize != 1)
        {
            // To make sure we execute at something over internal ranks in each iteration - even if its noop
            currentNumberOfSend = 1;
            currentNumberOfRecv = 1;
        }

        LOG_HCL_TRACE(HCL,
                      "iter={}, maxNumberOfSend={}, maxNumberOfRecv={}, "
                      "currentNumberOfSend={} currentNumberOfRecv={}, iter={}",
                      iter,
                      maxNumberOfSend,
                      maxNumberOfRecv,
                      currentNumberOfSend,
                      currentNumberOfRecv,
                      iter);

        // Fill collectiveParams with defaults
        HclCollectiveParams collectiveParams {m_device->getComm(comm)};
        CommonState         commonState {collectiveParams,
                                 m_intermediateBufferManager,
                                 isHnicsRequired,
                                 m_scaleoutProvider->isGaudiDirect(),
                                 m_device->getEdmaEngineWorkDistributionSize(),
                                 m_serverConnectivity.getMaxNumScaleUpPortsPerConnection(comm),
                                 m_device->getComm(comm).getCommConnectivity().getNumScaleOutPorts(),
                                 m_device->getSignalsCalculator(),
                                 this->m_remainderCalculator};
        commonState.initCurrentOp(eHCLNoCollective, 0, 0);
        m_signalsManager->initialize(&commonState, 0);
        m_activeStreamManager.initializeDmaStreams(commonState, myBoxNumInfo.m_boxNum);

        commonState.m_scaleoutNonCollectiveSend = scaleoutSendIter.size();
        commonState.m_scaleoutNonCollectiveRecv = scaleoutRecvIter.size();
        const unsigned requiredCredits          = calcRequiredCreditAmount(commonState,
                                                                  myBoxNumInfo,
                                                                  myBoxNumInfo,
                                                                  iter,
                                                                  0,
                                                                  0, /* firstBoxIter */
                                                                  true,
                                                                  commonState.m_collectiveOp,
                                                                  dependencyTargetVal);
        LOG_HCL_TRACE(HCL, "iter={}, requiredCredits={}", iter, requiredCredits);

        createScaleUpSendProgsNonCollective(currentNumberOfSend,
                                            currentNumberOfRecv,
                                            sendCnt,
                                            recvCnt,
                                            scaleupSendIter,
                                            iterMemcpyVec,
                                            iterScaleoutSignals,
                                            comm,
                                            requiredCredits,
                                            apiId);

        createScaleUpRecvProgsNonCollective(currentNumberOfRecv, scaleupRecvIter, comm, requiredCredits);

        createScaleOutSendProgsNonCollective(scaleoutSendIter,
                                             comm,
                                             requiredCredits,
                                             qpSetIterPerSendPeerRank,
                                             commonState);
        createScaleOutRecvProgsNonCollective(scaleoutRecvIter,
                                             comm,
                                             requiredCredits,
                                             qpSetIterPerRecvPeerRank,
                                             commonState);

        createDmaProgsNonCollective(0, requiredCredits);

        m_deviceController.submitWork(m_streamId);
    }

    m_device->getComm(comm).m_streamLatestLongSo[m_streamId] = m_longSo.targetValue;

    LOG_TRACE(HCL_CG,
              SCAL_PROGRESS_HCL_FMT "sendRecv: num iterations {}, longSo before {:x}",
              m_streamId,
              m_longSo.long_so_index,
              m_longSo.targetValue,
              numIterations,
              startTgtVal);

    return hcclSuccess;
}

void HclCollectiveRoutinesGen2Arch::initExec(HcclGraph* graph, int exec)
{
    auto commonState = graph->graphState();

    commonState->initCurrentOp(commonState->m_currentOp, exec, 0);
    commonState->m_syncUpBufferWithLtu = false;
    graph->sendSlice().m_boxIter = graph->recvSlice().m_boxIter = exec;

    uint64_t cuid = 0;
    m_activeStreamManager.initializeDmaStreams(*commonState, graph->sendSlice().m_boxNumInfo.m_boxNum);

    // New Api call -> inc the long SO
    advanceProg(false, cuid, commonState.get());
    LOG_HCL_DEBUG(HCL, "Initializing execution set submission. targetValue={}", m_longSo.targetValue);
}

void HclCollectiveRoutinesGen2Arch::finalizeExec(HcclGraph* graph, int exec)
{
    ScopedNullSubmit scopedNullSubmit(m_streamId, m_deviceController);

    bool     isFirstOp    = true;
    int      firstBoxIter = 0;
    unsigned sliceIter    = 0;

    auto     commonState         = graph->graphState();
    uint64_t dependencyTargetVal = 0;

    {
        LOG_HCL_CONTEXT_TRACE(HCL, "Registering static buffers");
        m_staticBuffersAllocator.registerStaticBuffersAllocations(*commonState, exec);
    }

    const unsigned requiredCredits = calcRequiredCreditAmount(*commonState,
                                                              graph->sendSlice().m_boxNumInfo,
                                                              graph->recvSlice().m_boxNumInfo,
                                                              sliceIter,
                                                              0xffffffff,
                                                              firstBoxIter,
                                                              isFirstOp,
                                                              commonState->m_currentOp,
                                                              dependencyTargetVal);
    LOG_HCL_DEBUG(HCL, "Required credits: {}", requiredCredits);

    uint64_t cuid = commonState->calculateCUID(false, false);

    graph->sendSlice().setScaleoutAddresses(*m_addressGenerator, 0);
    graph->recvSlice().setScaleoutAddresses(*m_addressGenerator, 0);

    m_signalsManager->allocateResources();
    m_signalsManager->updateCompletionTracker(m_longSo.targetValue, cuid);
    m_signalsManager->printGraph();

    unsigned completionSignals = m_signalsManager->getNumSignalsForCompletion();

    // The next calls are to fill the five sched with programs
    createScaleUpSendProgs(graph->sendSlice(),
                           sliceIter,
                           graph->context().m_myBoxNumInfo,
                           requiredCredits,
                           commonState->m_currentOp,
                           completionSignals);

    createScaleUpRecvProgs(graph->recvSlice(),
                           sliceIter,
                           graph->context().m_myBoxNumInfo,
                           requiredCredits,
                           commonState->m_currentOp);

    createScaleOutSendProgs(graph->sendSlice(), requiredCredits);
    createScaleOutRecvProgs(graph->recvSlice(), requiredCredits);

    createDmaProgs(graph->sendSlice(),
                   graph->recvSlice(),
                   graph->recvSlice().getChunkCountToClear() * dataTypeSizeInBytes(commonState->m_dataType),
                   requiredCredits,
                   commonState->m_dataType);

    commonState->m_dynamicComm.m_streamLatestLongSo[m_streamId] = m_longSo.targetValue;

    LOG_HCL_DEBUG(HCL, "finalizing exec {} submission", exec);
    m_deviceController.submitWork(m_streamId, true);
}

uint64_t HclCollectiveRoutinesGen2Arch::initGraph(HcclGraph* graph)
{
    m_deviceController.getStreamLock(m_streamId).lock();
    HclCollectiveParams& params = *(graph->graphParams());

    graph->context().m_state =
        std::make_shared<CommonState>(params,
                                      m_intermediateBufferManager,
                                      m_device->getEdmaEngineWorkDistributionSize(),
                                      m_serverConnectivity.getMaxNumScaleUpPortsPerConnection(params.m_dynamicComm),
                                      params.m_dynamicComm.getCommConnectivity().getNumScaleOutPorts(),
                                      m_device->getSignalsCalculator(),
                                      this->m_remainderCalculator);

    auto commonState = graph->graphState();

    commonState->m_sliceIterations = 1;
    commonState->m_currentOp = params.m_currentOp == eHCLNoCollective ? params.m_collectiveOp : params.m_currentOp;
    commonState->calcSliceQpSet(0);

    const unsigned box                       = commonState->m_dynamicComm.getMyScaleupGroup();
    graph->context().m_myBoxNumInfo.m_boxNum = box;

    graph->context().m_sendSliceState.reset(
        new SliceState(*commonState, commonState->m_currentOp, true, 0, {box, BoxNumInfo::boxOrientation::NEXT_BOX}));

    graph->context().m_recvSliceState.reset(
        new SliceState(*commonState, commonState->m_currentOp, false, 0, {box, BoxNumInfo::boxOrientation::PREV_BOX}));

    m_requestStrongOrderIter = graph->startStrongOrder;

    return m_longSo.targetValue;
}

void HclCollectiveRoutinesGen2Arch::finalizeGraph(HcclGraph* graph, uint64_t startTargetVal)
{
    auto commonState = graph->graphState();

    LOG_TRACE(HCL_CG,
              SCAL_PROGRESS_HCL_FMT "collective-op {} longSo before 0x{:x}",
              m_streamId,
              m_longSo.long_so_index,
              m_longSo.targetValue,
              commonState->m_collectiveOp,
              startTargetVal);

    m_signalsManager->finalize(true);
    m_deviceController.getStreamLock(m_streamId).unlock();
}

hcclResult_t HclCollectiveRoutinesGen2Arch::processSendPrim(HcclGraph* graph, HcclPrimSend* sendPrim)
{
    m_addressGenerator->setScaleoutSendAddr(sendPrim->m_sendAddr);

    LOG_HCL_CONTEXT_INFO(HCL,
                         "Processing scaleout send primitive to rank {} m_sendAddr=0x{:X}",
                         sendPrim->m_sendRank,
                         sendPrim->m_sendAddr);

    graph->sendSlice().updateScaleoutCounts(sendPrim->m_sendRank,
                                            sendPrim->sendCount(),
                                            m_scaleoutProvider->getInternalScaleoutFences());
    m_scaleoutProvider->validateSize(sendPrim->sendCount() * graph->sendSlice().m_dataTypeSizeInBytes);

    hcl::ScalStream& currentStream =
        m_activeStreamManager.getActiveCollectiveStream(hcl::SchedulersIndex::sendScaleOut);

    m_deviceController.setHostFences(m_streamId,
                                     currentStream.getUarchStreamIndex(),
                                     true,
                                     graph->sendSlice().m_setup.m_scaleoutInternalFences,
                                     graph->sendSlice().m_execution.m_scaleoutFences);

    WaitMethod waitMethod = sendPrim->getWaitResource(m_scaleoutProvider->isHostNic() || sendPrim->isSignaling());

    if (m_scaleoutProvider->isHostNic())
    {
        addScaleoutInternalSOB(graph->sendSlice(), waitMethod);
    }

    SignalEvent signalEvent = m_scaleoutProvider->getScaleoutSendSignal();

    if (sendPrim->isSignaling())
    {
        const int startPhase = m_signalsManager->getNextPhase(waitMethod);
        int       phase      = startPhase;
        for (hcclSyncInfo* wait_sync : sendPrim->waiters())
        {
            WaitEvent waitEvent = wait_sync->m_waiter->getWaitEvent();
            m_signalsManager->enqueueWait(waitEvent, {signalEvent}, waitMethod, phase, 1, 0, phase == startPhase);
            graph->sendSlice().m_execution.m_completionSoAddr = m_signalsManager->dequeueSoAddress(signalEvent);
            wait_sync->syncMethod                             = waitMethod;
            phase++;
        }
    }
    else
    {
        m_signalsManager->enqueueCompletion({signalEvent});
        graph->sendSlice().m_execution.m_completionSoAddr = m_signalsManager->dequeueSoAddress(signalEvent);
    }

    return hcclSuccess;
}

hcclResult_t HclCollectiveRoutinesGen2Arch::processRecvPrim(HcclGraph* graph, HcclPrimRecv* recvPrim)
{
    m_addressGenerator->setScaleoutRecvAddr(recvPrim->m_recvAddr);

    LOG_HCL_CONTEXT_INFO(HCL, "processing scaleout recv primitive, from rank {}", recvPrim->m_recvRank);

    graph->recvSlice().updateScaleoutCounts(recvPrim->m_recvRank,
                                            recvPrim->recvCount(),
                                            m_scaleoutProvider->getInternalScaleoutFences());
    m_scaleoutProvider->validateSize(recvPrim->recvCount() * graph->recvSlice().m_dataTypeSizeInBytes);

    hcl::ScalStream& currentStream =
        m_activeStreamManager.getActiveCollectiveStream(hcl::SchedulersIndex::recvScaleOut);

    m_deviceController.setHostFences(m_streamId,
                                     currentStream.getUarchStreamIndex(),
                                     false,
                                     graph->recvSlice().m_setup.m_scaleoutInternalFences,
                                     graph->recvSlice().m_execution.m_scaleoutFences);

    SignalEvent signalEvent = m_scaleoutProvider->getScaleoutRecvSignal();

    WaitMethod waitMethod = recvPrim->getWaitResource(m_scaleoutProvider->isHostNic() || recvPrim->isSignaling());
    m_scaleoutProvider->setInternalScaleoutRecvWait(waitMethod, *m_signalsManager);
    if (m_scaleoutProvider->isHostNic())
    {
        addScaleoutInternalSOB(graph->recvSlice(), waitMethod);
    }

    if (recvPrim->isSignaling())
    {
        const int start_phase = m_signalsManager->getNextPhase(waitMethod);
        int       phase       = start_phase;
        for (hcclSyncInfo* wait_sync : recvPrim->waiters())
        {
            WaitEvent waitEvent = wait_sync->m_waiter->getWaitEvent();
            m_signalsManager->enqueueWait(waitEvent, {signalEvent}, waitMethod, phase, 1, 0, phase == start_phase);
            wait_sync->syncMethod                             = waitMethod;
            graph->recvSlice().m_execution.m_completionSoAddr = m_signalsManager->dequeueSoAddress(signalEvent);
            phase++;
        }
    }
    else
    {
        m_signalsManager->enqueueCompletion({signalEvent});
        graph->recvSlice().m_execution.m_completionSoAddr = m_signalsManager->dequeueSoAddress(signalEvent);
    }

    return hcclSuccess;
}

hcclResult_t HclCollectiveRoutinesGen2Arch::processAgPrim(HcclGraph* graph, HcclPrimAllGather* agPrim)
{
    m_addressGenerator->setScaleupSendAddr(agPrim->sendAddr());
    m_addressGenerator->setScaleupRecvAddr(agPrim->recvAddr());

    LOG_HCL_CONTEXT_INFO(HCL,
                         "processing AG primitive, sendAddr=0x{:X} recvAddr=0x{:X} inputCount={}",
                         agPrim->sendAddr(),
                         agPrim->recvAddr(),
                         agPrim->inputCount());

    signalEvents_t signalEvents = agPrim->getSignalEvents();

    if (m_graphSync.isForceOrder(true))
    {
        m_signalsManager->enqueueCompletion({SignalEvent::FORCE_ORDER});
    }

    WaitMethod waitMethod = agPrim->getWaitResource(agPrim->isSignaling());
    if (agPrim->isSignaling())
    {
        const int start_phase = m_signalsManager->getNextPhase(waitMethod);
        int       phase       = start_phase;
        for (hcclSyncInfo* wait_sync : agPrim->waiters())
        {
            WaitEvent waitEvent = wait_sync->m_waiter->getWaitEvent();
            m_signalsManager
                ->enqueueWait(waitEvent, std::move(signalEvents), waitMethod, phase, 1, 0, phase == start_phase);
            wait_sync->syncMethod = waitMethod;
            phase++;
        }
    }
    else
    {
        m_signalsManager->enqueueCompletion(std::move(signalEvents));
    }

    agPrim->updateCounts();

    return hcclSuccess;
}

hcclResult_t HclCollectiveRoutinesGen2Arch::processBcastPrim(HcclGraph* graph, HcclPrimBroadcast* bcastPrim)
{
    m_addressGenerator->setScaleupSendAddr(bcastPrim->sendAddr());
    m_addressGenerator->setScaleupRecvAddr(bcastPrim->recvAddr());

    LOG_HCL_CONTEXT_INFO(HCL, "processing Bcast primitive.");

    signalEvents_t signalEvents = bcastPrim->getSignalEvents();

    if (m_graphSync.isForceOrder(true))
    {
        m_signalsManager->enqueueCompletion({SignalEvent::FORCE_ORDER});
    }

    WaitMethod waitMethod = bcastPrim->getWaitResource(bcastPrim->isSignaling());
    if (bcastPrim->isSignaling())
    {
        const int start_phase = m_signalsManager->getNextPhase(waitMethod);
        int       phase       = start_phase;
        for (hcclSyncInfo* wait_sync : bcastPrim->waiters())
        {
            WaitEvent waitEvent = wait_sync->m_waiter->getWaitEvent();
            m_signalsManager
                ->enqueueWait(waitEvent, std::move(signalEvents), waitMethod, phase, 1, 0, phase == start_phase);
            wait_sync->syncMethod = waitMethod;
            phase++;
        }
    }
    else
    {
        m_signalsManager->enqueueCompletion(std::move(signalEvents));
    }

    bcastPrim->updateCounts();

    return hcclSuccess;
}

hcclResult_t HclCollectiveRoutinesGen2Arch::hclCollectiveCall(HclCollectiveParams& params)
{
    ScopedNullSubmit scopedNullSubmit(m_streamId, m_deviceController);

    std::lock_guard<std::mutex> lock(m_deviceController.getStreamLock(m_streamId));

    CommonState commonState {params,
                             m_intermediateBufferManager,
                             m_scaleoutProvider->isHostNic(),
                             m_scaleoutProvider->isGaudiDirect(),
                             m_device->getEdmaEngineWorkDistributionSize(),
                             m_serverConnectivity.getMaxNumScaleUpPortsPerConnection(params.m_dynamicComm),
                             params.m_dynamicComm.getCommConnectivity().getNumScaleOutPorts(),
                             m_device->getSignalsCalculator(),
                             this->m_remainderCalculator};

    // handle a portion of data that fits the relevant slice in each iteration
    // slice: [0, 1, ..., numSlices - 1]
    const size_t   sliceLoop = commonState.m_sliceIterations;
    const uint64_t boxLoop   = commonState.m_boxIterations;

    uint64_t startTgtVal = m_longSo.targetValue;

    for (unsigned sliceIter = 0; sliceIter < commonState.m_sliceIterations; sliceIter++)
    {
        commonState.calcSliceCounts(sliceIter);
        commonState.calcSliceQpSet(sliceIter);
        // handle entire reduceScatter operation followed by allGather operation in case of a hierarchical allReduce
        if (commonState.m_collectiveOp == eHCLAllReduce || commonState.m_collectiveOp == eHCLReduce)
        {
            const HCL_CollectiveOp firstOp  = eHCLReduceScatter;
            const HCL_CollectiveOp secondOp = commonState.m_collectiveOp == eHCLAllReduce ? eHCLAllGather : eHCLGather;

            for (unsigned boxIter = 0; boxIter < commonState.m_boxIterations; ++boxIter)
            {
                hclCollectiveCall(commonState, sliceIter, boxIter, 0, 0, true, firstOp);
            }

            unsigned gatherStartBoxIter  = 0;
            unsigned gatherEndBoxIter    = commonState.m_boxIterations;
            unsigned scaleoutSendBoxIter = 0;
            if ((commonState.m_collectiveOp == eHCLReduce) && !commonState.isRootBox())
            {
                // Determine the exact single iteration that non-root boxes do the scaleout send
                scaleoutSendBoxIter = mod(commonState.m_boxIterations + commonState.rootBox() -
                                              commonState.m_dynamicComm.getMyScaleupGroup(),
                                          commonState.m_boxIterations);
                gatherStartBoxIter  = scaleoutSendBoxIter;
                gatherEndBoxIter    = scaleoutSendBoxIter + 1;  // exactly 1 iteration
            }

            for (unsigned boxIter = gatherStartBoxIter; boxIter < gatherEndBoxIter; ++boxIter)
            {
                hclCollectiveCall(commonState, sliceIter, boxIter, 0, gatherStartBoxIter, false, secondOp);
            }

            continue;
        }
        else if (commonState.m_collectiveOp == eHCLBroadcast)
        {
            // we only scatter in the root box and to the next box so we don't need more than 2 iterations
            for (unsigned boxIter = 0; boxIter < commonState.getBroadcastScatterOpBoxIterations(); ++boxIter)
            {
                hclCollectiveCall(commonState, sliceIter, boxIter, 0, 0, true, eHCLScatter);
            }

            // we run AG only within the box
            hclCollectiveCall(commonState, sliceIter, 0, 0, 0, false, eHCLAllGather);

            continue;
        }

        // handle a portion of data relevant for a specific box in each iteration
        // send: [myBox, myBox + 1, ..., myBox + (numBoxes - 1)]
        // recv: [myBox, myBox - 1, ..., myBox - (numBoxes - 1)]
        for (unsigned boxIter = 0; boxIter < commonState.m_boxIterations; ++boxIter)
        {
            switch (commonState.m_collectiveOp)
            {
                case eHCLAllReduce:
                    VERIFY(false);
                case eHCLAll2All:
                    for (unsigned all2allIter = 0; all2allIter < (boxIter > 0 ? commonState.m_all2allIterations : 1);
                         ++all2allIter)
                    {
                        hclCollectiveCall(commonState, sliceIter, boxIter, all2allIter, 0, true, eHCLAll2All);
                    }
                    break;
                case eHCLSinglePeerBroadcast:
                    hclCollectiveCall(commonState, sliceIter, boxIter, 0, 0, true, eHCLScatter);
                    hclCollectiveCall(commonState, sliceIter, boxIter, 0, 0, false, eHCLAllGather);
                    break;
                default:
                    hclCollectiveCall(commonState, sliceIter, boxIter, 0, 0, true, commonState.m_collectiveOp);
                    break;
            }
        }
    }

    m_signalsManager->finalize(true);

    LOG_TRACE(HCL_CG,
              SCAL_PROGRESS_HCL_FMT "#slices {} #boxes {} collective-op {} longSo before 0x{:x}",
              m_streamId,
              m_longSo.long_so_index,
              m_longSo.targetValue,
              sliceLoop,
              boxLoop,
              params.m_collectiveOp,
              startTgtVal);

    return hcclSuccess;
}

static uint64_t s_submitCounter = 0;

void HclCollectiveRoutinesGen2Arch::hclCollectiveCall(CommonState&     commonState,
                                                      unsigned         sliceIter,
                                                      unsigned         boxIter,
                                                      unsigned         all2allIter,
                                                      const unsigned   firstBoxIter,
                                                      bool             isFirstOp,
                                                      HCL_CollectiveOp currentOp)
{
    VERIFY(currentOp != eHCLCollectiveLastValue);
    commonState.initCurrentOp(currentOp, boxIter, all2allIter);

    const bool scaleOutFirstOp =
        ((commonState.m_currentOp == eHCLAllGather || commonState.m_currentOp == eHCLGather ||
          commonState.m_collectiveOp == eHCLBroadcast || commonState.m_collectiveOp == eHCLSinglePeerBroadcast ||
          commonState.m_collectiveOp == eHCLSimpleBroadcast) &&
         commonState.m_dynamicComm.getScaleupGroupSize() != 1);

    const unsigned nextBox = mod(commonState.m_dynamicComm.getMyScaleupGroup() + boxIter, commonState.m_boxIterations);

    const unsigned prevBox =
        mod(commonState.m_boxIterations + (int)commonState.m_dynamicComm.getMyScaleupGroup() - (int)boxIter,
            commonState.m_boxIterations);
    BoxNumInfo boxNumInfo =
        BoxNumInfo(scaleOutFirstOp ? prevBox : nextBox,
                   scaleOutFirstOp ? BoxNumInfo::boxOrientation::PREV_BOX : BoxNumInfo::boxOrientation::NEXT_BOX);
    BoxNumInfo nextBoxNumInfo(nextBox, BoxNumInfo::boxOrientation::NEXT_BOX);
    BoxNumInfo prevBoxNumInfo(prevBox, BoxNumInfo::boxOrientation::PREV_BOX);

    const bool isFirstBox = (boxNumInfo.m_boxNum == commonState.m_dynamicComm.getMyScaleupGroup());
    const bool isLastBox =
        (mod(boxNumInfo.m_boxNum + 1, commonState.m_boxIterations) == commonState.m_dynamicComm.getMyScaleupGroup());

    uint64_t cuid = commonState.calculateCUID(isFirstBox, isLastBox);

    uint64_t dependencyTargetVal = 0;

    // Check dependency per collective
    if (!GCFG_WEAK_ORDER.value() && GCFG_ENABLE_DEPENDENCY_CHECKER.value() && sliceIter == 0 && isFirstOp &&
        boxIter == firstBoxIter)
    {
        uint64_t totalBoxIterations = 0;
        if (commonState.m_collectiveOp == eHCLBroadcast)
        {
            // We only scatter in the root box and to the next box so we don't need more than 2 iterations + 1 AG
            totalBoxIterations = commonState.getBroadcastScatterOpBoxIterations() + 1;
        }
        else if (commonState.m_collectiveOp == eHCLReduce)
        {
            if (commonState.isRootBox())
            {
                // root box ranks do all RS and gather iterations
                totalBoxIterations = commonState.m_boxIterations * 2;
            }
            else
            {
                // non root box ranks do all RS iterations and 1 gather iterations
                totalBoxIterations = commonState.m_boxIterations + 1;
            }
        }
        else
        {
            // complex collectives (AR) do 2 operations, so (2 * #boxes)
            totalBoxIterations = (commonState.isComplexImplementation() ? 2 : 1) * commonState.m_boxIterations;
        }

        // Calculate the target value for the entire collective.
        uint64_t targetValue = m_longSo.targetValue + (commonState.m_sliceIterations * totalBoxIterations);

        dependencyTargetVal = checkCollectiveDependency(commonState, targetValue);
    }

    LOG_HCL_CONTEXT_INFO(
        HCL,
        "Running another iteration, collectiveOp={} currentOp={} sliceIter={} boxIter={} all2allIter={} targetValue={}",
        commonState.m_collectiveOp,
        commonState.m_currentOp,
        sliceIter,
        boxIter,
        all2allIter,
        m_longSo.targetValue + 1);
    LOG_HCL_DEBUG(HCL,
                  "More details about this iteration: nextBox={} prevBox={} firstBoxIter={}, will work on boxNum={}",
                  nextBox,
                  boxIter,
                  prevBox,
                  firstBoxIter,
                  boxNumInfo.m_boxNum);

    // New Api call -> inc the long SO
    advanceProg(false, cuid, &commonState);

    {
        LOG_HCL_CONTEXT_TRACE(HCL, "Registering static buffers");
        m_staticBuffersAllocator.registerStaticBuffersAllocations(commonState, boxIter);
    }

    const unsigned requiredCredits = calcRequiredCreditAmount(commonState,
                                                              nextBoxNumInfo,
                                                              prevBoxNumInfo,
                                                              sliceIter,
                                                              boxIter,
                                                              firstBoxIter,
                                                              isFirstOp,
                                                              commonState.m_currentOp,
                                                              dependencyTargetVal);
    LOG_HCL_DEBUG(HCL, "Required credits: {}", requiredCredits);

    SliceState sendSliceState {commonState,
                               *m_addressGenerator,
                               commonState.m_currentOp,
                               true,
                               sliceIter,
                               nextBoxNumInfo};
    SliceState recvSliceState {commonState,
                               *m_addressGenerator,
                               commonState.m_currentOp,
                               false,
                               sliceIter,
                               prevBoxNumInfo};

    m_activeStreamManager.initializeDmaStreams(commonState, sendSliceState.m_boxNumInfo.m_boxNum);

    if (!m_signalsManager->isGraphLoaded())
    {
        LOG_HCL_CONTEXT_TRACE(HCL, "Now calculating scaleup resources");
        calculateScaleupSignals(commonState, boxNumInfo, isLastBox, isFirstBox);
    }

    if (boxIter != 0)  // don't call scaleout functionality for boxIter=0
    {
        negotiateScaleoutResources(sendSliceState, isFirstBox, isLastBox);
        negotiateScaleoutResources(recvSliceState, isFirstBox, isLastBox);
    }

    m_signalsManager->allocateResources();
    m_signalsManager->updateCompletionTracker(m_longSo.targetValue, cuid);
    m_signalsManager->printGraph();

    unsigned completionSignals = m_signalsManager->getNumSignalsForCompletion();

    // The next calls are to fill the five sched with programs
    createScaleUpSendProgs(sendSliceState.isComplexImplementation() && sendSliceState.m_currentOp != eHCLReduceScatter
                               ? recvSliceState
                               : sendSliceState,
                           sliceIter,
                           boxNumInfo,
                           requiredCredits,
                           commonState.m_currentOp,
                           completionSignals);
    createScaleUpRecvProgs(recvSliceState.isComplexImplementation() && recvSliceState.m_currentOp == eHCLReduceScatter
                               ? sendSliceState
                               : recvSliceState,
                           sliceIter,
                           boxNumInfo,
                           requiredCredits,
                           commonState.m_currentOp);

    createScaleOutSendProgs(sendSliceState, requiredCredits);
    createScaleOutRecvProgs(recvSliceState, requiredCredits);

    createDmaProgs(sendSliceState,
                   recvSliceState,
                   recvSliceState.getChunkCountToClear() * dataTypeSizeInBytes(commonState.m_dataType),
                   requiredCredits,
                   commonState.m_dataType);

    bool submitToHw = true;

    if (GCFG_HCL_SUBMIT_THRESHOLD.isSetFromUserConfig() && m_scaleoutProvider->isGaudiDirect() &&
        sendSliceState.m_isMultiScaleupGroup)
    {
        commonState.m_submitCounter++;
        bool lastIterInCollective = ((sendSliceState.m_sliceIter + 1) == sendSliceState.m_sliceIterations &&
                                     (sendSliceState.m_boxIter + 1) == sendSliceState.m_boxIterations);
        bool all2allLastIter      = (sendSliceState.m_collectiveOp != eHCLAll2All ||
                                (sendSliceState.m_all2allIter + 1) == sendSliceState.m_all2allIterations);
        submitToHw                = ((lastIterInCollective && all2allLastIter) ||
                      commonState.m_submitCounter == GCFG_HCL_SUBMIT_THRESHOLD.value());
        if (submitToHw)
        {
            s_submitCounter = 0;
        }

        LOG_HCL_TRACE(HCL,
                      "submitCounter={}, submitToHw={}, lastIterInCollective={}, all2allLastIter={}",
                      commonState.m_submitCounter,
                      submitToHw,
                      lastIterInCollective,
                      all2allLastIter);
    }

    m_deviceController.submitWork(m_streamId, submitToHw);

    commonState.m_dynamicComm.m_streamLatestLongSo[m_streamId] = m_longSo.targetValue;
}

void HclCollectiveRoutinesGen2Arch::negotiateScaleoutResources(SliceState& sliceState, bool isFirstBox, bool isLastBox)
{
    LOG_HCL_CONTEXT_TRACE(HCL, "Now negotiating scaleout {} resources...", sliceState.m_isSend ? "send" : "recv");
    if (!m_signalsManager->isGraphLoaded())
    {
        determineCompletionSO(sliceState, isFirstBox, isLastBox);
    }
    m_scaleoutProvider->requestScaleoutResources(sliceState, *m_signalsManager);
    provideScaleoutResources(sliceState);
}

uint64_t HclCollectiveRoutinesGen2Arch::checkSendRecvDependency(uint64_t address,
                                                                uint64_t size,
                                                                uint64_t targetValue,
                                                                bool     isSend,
                                                                bool     dbModificationIsAllowed)
{
    if (isSend)
    {
        return m_dependencyChecker->getTargetValueForReadRange(address, size, targetValue, dbModificationIsAllowed);
    }
    else
    {
        return m_dependencyChecker->getTargetValueForWriteRange(address, size, targetValue, dbModificationIsAllowed);
    }
}

uint64_t HclCollectiveRoutinesGen2Arch::checkCollectiveDependency(CommonState& commonState,
                                                                  uint64_t     targetValue,
                                                                  bool         dbModificationIsAllowed)
{
    uint64_t dependencyTargetVal = 0;

    if (commonState.m_inPlace && commonState.m_collectiveOp == eHCLReduceScatter)
    {
        // Special case: Inplace, RS and scaleout - we use SendBuff to store partial results for scaleout,
        // so in this case the Input rank is treated as write, for simplicity all RS inplace will be treated this way
        dependencyTargetVal = m_dependencyChecker->getTargetValueForWriteRange(commonState.m_sendBufferAddr,
                                                                               commonState.calcSendAddrSize(),
                                                                               targetValue,
                                                                               dbModificationIsAllowed);
    }
    else
    {
        uint64_t dependencyRecvTargetVal = 0;
        if (commonState.isRecvAddrValid())
        {
            dependencyRecvTargetVal = m_dependencyChecker->getTargetValueForWriteRange(commonState.m_recvBufferAddr,
                                                                                       commonState.calcRecvAddrSize(),
                                                                                       targetValue,
                                                                                       dbModificationIsAllowed);
        }

        // First the sendAddr should be valid (for reduce non-root it's not valid)
        // For Reduce - Non-root - Only Send Address is valid
        // For Reduce - Root - m_inPlace condition doesn't calculate correctly (should be fixed), so we compare sendAddr
        //                     to recvAddr to check Inplace
        // For the rest - check sendAddr only if not inplace
        if (commonState.isSendAddrValid() &&
            (((commonState.m_collectiveOp == eHCLReduce) &&
              (!commonState.isRoot() || /*Root*/ (commonState.m_sendBufferAddr != commonState.m_recvBufferAddr))) ||
             (commonState.m_collectiveOp != eHCLReduce && !commonState.m_inPlace)))
        {
            dependencyTargetVal = m_dependencyChecker->getTargetValueForReadRange(commonState.m_sendBufferAddr,
                                                                                  commonState.calcSendAddrSize(),
                                                                                  targetValue,
                                                                                  dbModificationIsAllowed);
        }

        if (dependencyRecvTargetVal > dependencyTargetVal)
        {
            dependencyTargetVal = dependencyRecvTargetVal;
        }
    }

    return dependencyTargetVal;
}
