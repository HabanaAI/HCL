#include "platform/gaudi3/hcl_device.h"

#include <memory>   // for make_shared, make_unique
#include <utility>  // for pair
#include <numeric>

#include "platform/gen2_arch_common/hcl_device_config.h"  // for HclDeviceConfig
#include "hcl_dynamic_communicator.h"                     // for HclDyna...
#include "hcl_global_conf.h"                              // for GCFG_MA...
#include "hcl_types.h"                                    // for HclConfigType, eIbvNicPhysicalState
#include "hcl_utils.h"                                    // for VERIFY
#include "infra/scal/gaudi3/scal_manager.h"               // for Gaudi3S...
#include "infra/scal/gen2_arch_common/scal_manager.h"     // for Gen2Arc...
#include "platform/gaudi3/commands/hcl_commands.h"        // for HclComm...
#include "platform/gen2_arch_common/eq_handler.h"         // for IEventQ...
#include "platform/gaudi3/hcl_graph_sync.h"
#include "platform/gaudi3/qp_manager.h"                     // for QPManager
#include "platform/gaudi3/simb_pool_container_allocator.h"  // for SimbP...
#include "platform/gen2_arch_common/scaleout_provider.h"
#include "ibverbs/hcl_ibverbs.h"
#include "interfaces/hcl_hal.h"                        // for HalPtr
#include "platform/gen2_arch_common/server_def.h"      // for Gen2ArchServerDef
#include "platform/gaudi3/signals/calculator.h"        // for SignalsCalculatorGaudi3
#include "hcl_dynamic_communicator.h"                  // for HclDynamicCommunicator
#include "hccl_context.h"                              // for hccl_context
#include "platform/gaudi3/nics_events_handler_impl.h"  // for NicsEventsHandlerGaudi3
#include "hccl_communicator.h"
#include "fault_tolerance_inc.h"  // for HLFT.* macros
#include "hccl_api_inc.h"         // for g_faultsCheckStopApi

class QPManagerScaleOutGaudi3;

// TLS
thread_local CommsSet HclDeviceGaudi3::s_faultToleranceGroupScaleoutComms = {};

/* tests only constructor */
HclDeviceGaudi3::HclDeviceGaudi3(HclDeviceControllerGen2Arch& controller,
                                 [[maybe_unused]] const int   moduleId,
                                 HclDeviceConfig&             deviceConfig,
                                 Gen2ArchServerDef&           serverDef)
: HclDeviceGen2Arch(true, controller, deviceConfig, serverDef)
{
    registerOpenQpCallback(LOOPBACK, [&](HCL_Comm comm) { return openQpsLoopback(comm); });
    registerOpenQpCallback(HLS3, [&](HCL_Comm comm) { return openQpsHLS(comm); });
    registerOpenQpCallback(HL338, [&](HCL_Comm comm) { return openQpsHLS(comm); });
    registerOpenQpCallback(HL3_RACK, [&](HCL_Comm comm) { return openQpsHLS(comm); });
    setHal(serverDef.getHalSharedPtr());
}

// Runtime ctor
HclDeviceGaudi3::HclDeviceGaudi3(HclDeviceControllerGen2Arch& controller,
                                 HclDeviceConfig&             deviceConfig,
                                 [[maybe_unused]] hcl::HalPtr halShared,
                                 Gen2ArchServerDef&           serverDef)
: HclDeviceGen2Arch(controller, deviceConfig, serverDef)
{
    // Read box type and create server specific objects
    const HclConfigType configType = (HclConfigType)GCFG_BOX_TYPE_ID.value();
    setHal(serverDef.getHalSharedPtr());
    if ((configType == HLS3) || (configType == LOOPBACK))
    {
    }
    else if (configType == HL338)
    {
        m_boxConfigType = HL338;
    }
    else if (configType == HL3_RACK)
    {
        m_boxConfigType = HL3_RACK;
    }
    else
    {
        VERIFY(false, "Invalid server type {} for G3 device", configType);
    }
    LOG_HCL_INFO(HCL, "Set server type to {}", m_boxConfigType);

    m_scalManager.getHBMAddressRange(m_allocationRangeStart, m_allocationRangeEnd);
    registerOpenQpCallback(LOOPBACK, [&](HCL_Comm comm) { return openQpsLoopback(comm); });
    registerOpenQpCallback(HLS3, [&](HCL_Comm comm) { return openQpsHLS(comm); });
    registerOpenQpCallback(HL338, [&](HCL_Comm comm) { return openQpsHLS(comm); });
    registerOpenQpCallback(HL3_RACK, [&](HCL_Comm comm) { return openQpsHLS(comm); });

    updateDisabledPorts();
    initNicsMask();
    openWQs();

    m_nicsEventsHandler = std::make_unique<NicsEventsHandlerGaudi3>(getServerConnectivityGaudi3(), *this);
    m_eqHandler         = new IEventQueueHandler(*m_nicsEventsHandler);
    m_eqHandler->startThread(this);
    // The scaleout mode is set according also to if all scaleout ports are disabled by LKD/HCL or not. This is
    // regardless of communicator setup.
    setScaleoutMode(getServerConnectivity().getNumScaleOutPortsGlbl());
    m_sibContainerManager = std::make_unique<SimbPoolContainerAllocatorGaudi3>(m_hal->getMaxArchStreams());
    m_sibContainerManager->init();
    createOfiPlugin();
    m_scaleoutProvider = ScaleoutProvider::createScaleOutProvider(this);
    setEdmaEngineGroupSizes();
    m_signalsCalculator = std::make_unique<SignalsCalculatorGaudi3>();
}

hlthunk_device_name HclDeviceGaudi3::getDeviceName()
{
    return HLTHUNK_DEVICE_GAUDI3;
}

void HclDeviceGaudi3::addQPsToQPManagerDB(const HCL_Comm   comm,
                                          const HCL_Rank   remoteRank,
                                          const QpsVector& qps,
                                          const size_t     nic)
{
    const QPManagerHints hints(comm, remoteRank);

    getComm(comm).m_qpManagers.at(nic)->addQPsToQPManagerDB(hints, qps);
}

void HclDeviceGaudi3::setDefaultScaleUpPortQPWithNicOffsets(hcl::ScalStream& stream,
                                                            const HCL_Comm   comm,
                                                            const bool       isSend)
{
    const uint16_t defaultScaleUpPort = getServerConnectivity().getDefaultScaleUpPort(comm);
    getComm(comm).m_qpManagers.at(defaultScaleUpPort)->setNicOffsetsAndLastRank(stream, comm, isSend);
}

QPUsage HclDeviceGaudi3::getBaseQpAndUsage(const HclDynamicCommunicator& dynamicComm,
                                           HCL_CollectiveOp              collectiveOp,
                                           bool                          isSend,
                                           bool                          isComplexCollective,
                                           bool                          isReductionInIMB,
                                           bool                          isHierarchical,
                                           uint64_t                      count,
                                           uint64_t                      cellCount,
                                           HclConfigType                 boxType,
                                           bool                          isScaleOut,
                                           HCL_Rank                      remoteRank,
                                           uint8_t                       qpSet,
                                           const bool                    isReduction,
                                           HCL_CollectiveOp              complexCollective,
                                           bool                          isRoot)
{
    const unsigned nic = isScaleOut ? getServerConnectivity().getDefaultScaleOutPortByIndex()
                                    : getServerConnectivity().getDefaultScaleUpPort(dynamicComm);
    return dynamicComm.m_qpManagers.at(nic)->getBaseQpAndUsage(dynamicComm,
                                                               collectiveOp,
                                                               isSend,
                                                               isComplexCollective,
                                                               isReductionInIMB,
                                                               isHierarchical,
                                                               count,
                                                               cellCount,
                                                               boxType,
                                                               isScaleOut,
                                                               remoteRank,
                                                               qpSet,
                                                               isReduction,
                                                               complexCollective,
                                                               isRoot);
}

void HclDeviceGaudi3::handleScaleoutNicStatusChange(const uint16_t nic, const bool up)
{
    const uint16_t port = getLogicalScaleoutPortNum(nic);
    HLFT_WRN("Scaleout nic={} {}, scaleoutPortNum={}", nic, up ? "up" : "shutdown", port);

    if (!up && m_delayedReports[port].cancel())
    {
        // cancel() returned true -> we had an outstanding NIC_UP event, and we removed it
        HLFT_TRC("canceled port {} up event", port);
        return;
    }

    if (m_failedScaleOutPortsMask[port] != up)
    {
        // If we are here, then we already know about the new state. For shutdown (!up), we don't expect double
        // notification, log an error. For "up", we can get multiple notifications, as up is sent also in case of a port
        // down (not only for shutdown), and port toggling will cause it. In this case log an info message.
        if (!up)
        {
            HLFT_ERR("port={} is already marked shutdown, not expected to get another notification", port);
        }
        else
        {
            HLFT_INF("port={} is already marked up. Another notification can happen because of port toggling", port);
        }

        // In both cases there is nothing to do
        return;
    }

    if (up)
    {
        if (m_delayedReports[port].cancel())
        {
            // cancel() returned true -> we had an outstanding NIC_UP event for this nic
            // LKD bug ( SW-22312 ) sometimes we get NIC_UP event twice
            // but we can tolerate this (mis)behavior
            HLFT_WRN("LKD reported port {} up event twice, reseting the timer", port);
        }

        int64_t delay_ms = 1000 * GCFG_HCL_FAULT_TOLERANCE_FAILBACK_DELAY.value();
        HLFT_TRC("delaying report of port {} up event for {} ms", nic, delay_ms);

        m_delayedReports[port].delay_execution([=]() { m_nicReports.add(port, true); }, delay_ms);
        return;
    }

    m_nicReports.add(port, false);
}

bool HclDeviceGaudi3::isSender(unsigned _qpi)
{
    return ((_qpi == G3::QP_e::QPE_RS_SEND) || (_qpi == G3::QP_e::QPE_AG_SEND) || (_qpi == G3::QP_e::QPE_A2A_SEND));
}

uint32_t HclDeviceGaudi3::getQpi(HCL_Comm comm, uint8_t nic, HCL_Rank remoteRank, uint32_t qpn, uint8_t qpSet)
{
    const QPManagerHints hints(comm, remoteRank, nic, INVALID_QP, qpn, qpSet);

    return getComm(comm).m_qpManagers.at(nic)->getQPi(hints);
}

uint32_t HclDeviceGaudi3::requestCollectiveQpnFromLKD(bool isScaleOut)
{
    return g_ibv.reserve_collective_qp(isScaleOut);
}

void HclDeviceGaudi3::reserveRankQps(HCL_Comm comm, const bool isScaleOut, const HCL_Rank remoteRank, QpsVector& qpnArr)
{
    LOG_HCL_TRACE(HCL,
                  "comm={}, isScaleOut={}, myRank={}, remoteRank={}",
                  comm,
                  isScaleOut,
                  getMyRank(comm),
                  remoteRank);
    uint8_t qpSets = getNumQpSets(isScaleOut, comm, remoteRank);
    for (uint8_t qpSet = 0; qpSet < qpSets; qpSet++)
    {
        for (uint64_t i = 0; i < getHal().getMaxQPsPerNic(); i++)
        {
            // for non-peers - we only need to open the RS qps since they are used for send receive
            // for scale out peers - we need 4 qps only RS and AG, A2A will be directed to use RS
            // for scale up - we open 6 qps (G3::QP_e)
            // for null-submit mode - we don't open QPs
            bool isPeer = getComm(comm).isPeer(remoteRank);
            if ((isScaleOut && ((!isPeer && !QPManagerGaudi3ScaleOut::isRsQp(i)) ||
                                (isPeer && QPManagerGaudi3ScaleOut::isA2AQp(i)))) ||
                GCFG_HCL_NULL_SUBMIT.value())

            {
                qpnArr.push_back(0);
            }
            else
            {
                qpnArr.push_back(requestCollectiveQpnFromLKD(isScaleOut));
            }

            LOG_HCL_DEBUG(HCL,
                          "Allocate QP, Comm={}, remoteRank({}){} qpSet: {}, QPi: {}, QPn: {}",
                          comm,
                          remoteRank,
                          remoteRank == getMyRank(comm) ? " Loopback connection, " : "",
                          qpSet,
                          i,
                          qpnArr[(qpSet * getHal().getMaxQPsPerNic()) + i]);
        }
    }

    const unsigned nic = isScaleOut ? getServerConnectivity().getDefaultScaleOutPortByIndex()
                                    : getServerConnectivity().getDefaultScaleUpPort(comm);
    addQPsToQPManagerDB(comm, remoteRank, qpnArr, nic);
}

uint32_t HclDeviceGaudi3::createQpnInLKD(HCL_Comm comm, const uint32_t nic, const unsigned qpId, uint32_t coll_qpn)
{
    auto offs = getNicToQpOffset(nic);

    return g_ibv.create_qp(comm, isSender(qpId), nic, coll_qpn + offs);
}

void HclDeviceGaudi3::createRankQps(HCL_Comm    comm,
                                    HCL_Rank    rank,
                                    nics_mask_t nics,
                                    QpsVector&  qpnArr,
                                    const bool  isScaleOut)
{
    LOG_HCL_TRACE(HCL,
                  "Processing comm={} rank={}, nics=0x{:x}, isScaleOut={}",
                  comm,
                  rank,
                  (uint64_t)(nics),
                  isScaleOut);

    uint8_t qpSets = getNumQpSets(isScaleOut, comm, rank);

    // loop over the active nics
    for (auto nic : nics)
    {
        createNicQps(comm, rank, nic, qpnArr, qpSets);
    }

    updateRankHasQp(comm, rank);
}

/**
 * @brief open QPs in loopback mode, use remote ranks QP data
 */
void HclDeviceGaudi3::createRankQpsLoopback(HCL_Comm comm, HCL_Rank rank, QpsVector& qpnArr)
{
    HCL_Rank myRank = getMyRank(comm);
    LOG_HCL_TRACE(HCL, "Processing my rank={} to rank={}", myRank, rank);

    // loop over nics
    for (uint16_t index = 0; index < COMPACT_RANK_INFO_NICS; index++)
    {
        uint32_t nic    = getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs.qp[index].nic;
        uint8_t  qpSets = getNumQpSets(isScaleOutPort(nic), comm, myRank);
        createNicQps(comm, rank, nic, qpnArr, qpSets);
    }

    // loopback always
    updateRankHasQp(comm, myRank);
}

/**
 * @brief create all QP sets/QPs on a single nic
 */
void HclDeviceGaudi3::createNicQps(HCL_Comm comm, HCL_Rank rank, uint8_t nic, QpsVector& qpnArr, uint8_t qpSets)
{
    // check if nic is down, we can open qps only for active nics
    if (!(m_hclNic.mask[nic]))
    {
        return;
    }

    for (uint8_t qpSet = 0; qpSet < qpSets; qpSet++)
    {
        uint8_t qpSetBase = getHal().getMaxQPsPerNic() * qpSet;
        // allocate max QPs per nic
        for (unsigned i = 0; i < getHal().getMaxQPsPerNic(); i++)
        {
            uint8_t qpnArrIndex = qpSetBase + i;
            if (qpnArr[qpnArrIndex] == 0) continue;
            uint32_t qpnWithOffset = createQpnInLKD(comm, nic, i, qpnArr[qpnArrIndex]);

            getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs[nic].qp[qpSet][i] = qpnWithOffset;

            LOG_HCL_TRACE(HCL,
                          "Allocate QP, comm {} remoteRank({}){} nic: {} qpSet: {}, qpn: {}",
                          comm,
                          rank,
                          getMyRank(comm) == rank ? " Loopback connection, " : "",
                          nic,
                          qpSet,
                          qpnWithOffset);
        }
    }
}

#define ACTIVE_NICS(rank) getActiveNics(getMyRank(comm), rank, 1, comm)

hcclResult_t HclDeviceGaudi3::openQpsHlsScaleUp(HCL_Comm comm)
{
    LOG_HCL_DEBUG(HCL, "comm={}", comm);

    // comm is scale-out only, no need for internal QPs
    if (getComm(comm).getInnerRanksExclusive().size() == 0)
    {
        return hcclSuccess;
    }

    QpsVector qpnArr;
    reserveRankQps(comm, false, HCL_INVALID_RANK, qpnArr);

    // in null-submit mode don't open QPs
    if (unlikely(GCFG_HCL_NULL_SUBMIT.value()))
    {
        return hcclSuccess;
    }

    // loop over all scale up ranks
    for (auto& rank : getComm(comm).getInnerRanksExclusive())
    {
        createRankQps(comm, rank, ACTIVE_NICS(rank), qpnArr, false);
    }

    return hcclSuccess;
}

hcclResult_t HclDeviceGaudi3::openQpsScaleOut(HCL_Comm comm, const UniqueSortedVector& outerRanks)
{
    LOG_HCL_DEBUG(HCL, "comm={}, outerRanks={}", comm, outerRanks);

    // loop over all outer ranks
    for (auto& rank : outerRanks)
    {
        QpsVector qpnArr;
        reserveRankQps(comm, true, rank, qpnArr);

        // in null-submit mode don't open QPs
        if (likely(!GCFG_HCL_NULL_SUBMIT.value()))
        {
            createRankQps(comm, rank, ACTIVE_NICS(rank), qpnArr, true);
        }
    }

    return hcclSuccess;
}

void HclDeviceGaudi3::createMigrationQps(const HCL_Comm commId, const uint16_t nicDown)
{
    HclDynamicCommunicator& dynamicComm  = getComm(commId);
    nics_mask_t             scaleoutMask = dynamicComm.getCommConnectivity().getScaleOutPorts();

    const uint16_t fromPort = getServerDef().getServerConnectivityConst().getScaleoutNicFromSubPort(nicDown);

    scaleoutMask.clear(fromPort);             // reset failed port's bit in mask
    const uint16_t toPort = scaleoutMask(0);  // get first set bit
    // translate to nic index
    const uint16_t toSubPortIndex = getServerDef().getServerConnectivityConst().getSubPortIndex(toPort);

    LOG_HCL_INFO(HCL,
                 "comm {}, nic down {} port {} mask 0x{:x} new nic {}, new nic index {}",
                 commId,
                 nicDown,
                 fromPort,
                 (uint64_t)scaleoutMask,
                 toPort,
                 toSubPortIndex);
    openScaleOutMigrationQps(commId, fromPort, toPort);
}

void HclDeviceGaudi3::openScaleOutMigrationQps(const HCL_Comm comm, const uint16_t fromPort, const uint16_t toPort)
{
    std::array<std::array<migration_qp_data_t, MAX_QPS_PER_CONNECTION_G3>, MAX_QPS_SETS_PER_CONNECTION> qpnData;

    UniqueSortedVector outerRanks;
    getOuterRanks(comm, outerRanks);
    getComm(comm).m_migrationQpManager.allocateMigrationQPDBStorage(comm, fromPort, getComm(comm).getCommSize());
    for (const auto rank : outerRanks)
    {
        QPManagerHints hints(comm, rank, fromPort, INVALID_QP, INVALID_QP, INVALID_QP);

        getComm(comm).m_backupRankQPs[rank][toPort] = getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs[toPort];
        for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
        {
            hints.m_qpSet = qpSet;
            for (unsigned qpi = 0; qpi < MAX_QPS_PER_CONNECTION_G3; qpi++)
            {
                qpnData[qpSet][qpi].oldNic = fromPort;
                qpnData[qpSet][qpi].newNic = toPort;
                hints.m_qpi                = qpi;
                uint32_t inactiveQpn =
                    getComm(comm).m_qpManagers[fromPort]->getQPn(hints);  // add offset after invalid check
                if (inactiveQpn != INVALID_QP)
                {
                    inactiveQpn += getNicToQpOffset(fromPort);

                    const uint32_t qpn = g_ibv.create_migration_qp(comm, isSender(qpi), toPort, fromPort, inactiveQpn);
                    hints.m_qpn        = qpn;
                    qpnData[qpSet][qpi].qpn = qpn;

                    getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs[toPort].qp[qpSet][qpi] = qpn;
                    LOG_HCL_TRACE(HCL,
                                  "New migration QP: comm {}, from nic {} to nic {} (base {}) rank {},"
                                  " qp set {}, qpi {}, qpn {}; replacing {}",
                                  comm,
                                  fromPort,
                                  toPort,
                                  getNicToQpOffset(toPort),
                                  rank,
                                  qpSet,
                                  qpi,
                                  qpn,
                                  inactiveQpn);
                }
                else
                {
                    qpnData[qpSet][qpi].qpn = INVALID_QP;
                }
            }
        }
        getComm(comm).m_migrationQpManager.addMigrationQpsToQPManagerDB(hints, qpnData);
    }
}

hcclResult_t HclDeviceGaudi3::openQpsLoopback(HCL_Comm comm)
{
    HclConfigType configType = (HclConfigType)GCFG_BOX_TYPE_ID.value();
    if (configType != LOOPBACK)
    {
        LOG_HCL_ERR(HCL, "Invalid config type ({}), expecting LOOPBACK ({})", configType, LOOPBACK);
        return hcclInvalidArgument;
    }

    LOG_HCL_DEBUG(HCL, "comm={}", comm);

    // initialize nic-index mapping
    initRemoteNicsLoopback(comm);

    // open scaleup QPs
    QpsVector scaleupQPArr;
    reserveRankQps(comm, false, HCL_INVALID_RANK, scaleupQPArr);
    for (uint8_t rank = 0; rank < GCFG_LOOPBACK_SCALEUP_GROUP_SIZE.value(); rank++)
    {
        createRankQpsLoopback(comm, rank, scaleupQPArr);
    }

    // open scaleout QPs
    for (auto& rank : getComm(comm).getOuterRanksExclusive())
    {
        QpsVector scaleoutQPArr;
        reserveRankQps(comm, true, rank, scaleoutQPArr);
        createRankQpsLoopback(comm, rank, scaleoutQPArr);
    }

    return hcclSuccess;
}

unsigned HclDeviceGaudi3::getSenderWqeTableSize()
{
    return m_cgSize;
}

unsigned HclDeviceGaudi3::getReceiverWqeTableSize()
{
    return m_cgSize;
}

hcclResult_t HclDeviceGaudi3::connectCommQps(HCL_Comm comm)
{
    hcclResult_t            rc          = hcclSuccess;
    HclDynamicCommunicator& dynamicComm = getComm(comm);
    LOG_INFO(HCL, "Update scale-up QPs");
    for (auto& rank : dynamicComm.getInnerRanksExclusive())
    {
        rc = connectRankQps(comm, rank);
        VERIFY(rc == hcclSuccess, "connectRankQps failed rc={}", rc);
    }

    LOG_INFO(HCL, "Update scale-out connections");
    m_scaleoutProvider->verifyConnections(comm);

    // call ServerConnectivity comm init before scal config QPs
    // as scal is using the ServerConnectivity ports mapping
    getServerConnectivity().onCommInit(dynamicComm);
    if (dynamicComm.commScaleupGroupHasMultipleRanks()) configQps(comm);
    return rc;
}

void HclDeviceGaudi3::configQps(const HCL_Comm comm)
{
    setInitialQpConfiguration(comm, true);
    setInitialQpConfiguration(comm, false);
}

/**
 * @brief set the scaleup qps nic offsets and the last rank.
 */
void HclDeviceGaudi3::setInitialQpConfiguration(const HCL_Comm comm, const bool isSend)
{
    constexpr unsigned          qpArchStreamIdx = 0;
    std::lock_guard<std::mutex> lock(hccl_device()->m_deviceController.getStreamLock(qpArchStreamIdx));

    hcl::Gaudi3ScalManager* g3ScalManager = dynamic_cast<hcl::Gaudi3ScalManager*>(&getScalManager());
    LOG_HCL_DEBUG(HCL_SCAL,
                  "configuring {} Qps, m_configurationCount={}",
                  isSend ? "send" : "recv",
                  g3ScalManager->getConfigurationCount());

    HclCommandsGaudi3&   gaudi3Commands = (HclCommandsGaudi3&)(getGen2ArchCommands());
    HclGraphSyncGaudi3   graphSync(0, gaudi3Commands);
    hcl::SchedulersIndex sched        = isSend ? hcl::SchedulersIndex::sendScaleUp : hcl::SchedulersIndex::recvScaleUp;
    uint64_t             soAddressLSB = g3ScalManager->getInitCgNextSo();

    hcl::ScalStream& stream = m_deviceController.getScalArbUarchStream(qpArchStreamIdx, sched);
    stream.setTargetValue(0);

    hcl::Gen2ArchScalWrapper::CgComplex cgComplex = g3ScalManager->getCgInfo("network_scaleup_init_completion_queue");
    // Alloc Barrier
    for (auto scheduler : initCgSchedList)
    {
        unsigned&        cgIdx     = cgComplex.cgInfo.cgIdx[(int)scheduler];
        hcl::ScalStream& arbStream = m_deviceController.getScalArbUarchStream(qpArchStreamIdx, scheduler);
        gaudi3Commands.serializeAllocBarrierCommand(arbStream, (int)scheduler, cgIdx, 1);
    }

    // set the SO to the correct value 0x400-0x1
    gaudi3Commands.serializeLbwWriteCommand(stream,
                                            (unsigned)sched,
                                            soAddressLSB,
                                            graphSync.getSoConfigValue(COMP_SYNC_GROUP_CMAX_TARGET - 1, true));

    // for null submission, disable Scal write for QP's config since they are 0
    if (GCFG_HCL_NULL_SUBMIT.value())
    {
        LOG_HCL_TRACE(HCL_SCAL, "calling disableCcb(true)");
        g3ScalManager->disableCcb(qpArchStreamIdx, true);
    }

    // add qp configuration commands to the cyclic buffer
    setDefaultScaleUpPortQPWithNicOffsets(stream, comm, isSend);

    if (GCFG_HCL_NULL_SUBMIT.value())
    {
        LOG_HCL_TRACE(HCL_SCAL, "calling disableCcb(false)");
        g3ScalManager->disableCcb(qpArchStreamIdx, false);
    }

    // Increment the SO to free the barrier
    gaudi3Commands.serializeLbwWriteCommand(stream, (unsigned)sched, soAddressLSB, graphSync.getSoConfigValue(1, true));

    // submit to FW
    stream.submit();

    // Wait for completion
    g3ScalManager->waitOnCg(cgComplex, g3ScalManager->getConfigurationCount() + 1);
}

void HclDeviceGaudi3::updateDisabledPorts()
{
    const uint64_t disabledPortsMap = ~(getServerConnectivity().getEnabledPortsMask());
    const uint64_t disabledPortsMapLoopback =
        GCFG_LOOPBACK_DISABLED_NICS.value().empty() ? 0 : getServerConnectivity().getExternalPortsMaskGlbl();

    m_deviceConfig.updateDisabledPorts(disabledPortsMap, disabledPortsMapLoopback);
}

void HclDeviceGaudi3::getLagInfo(const uint16_t nic, uint8_t& lagIdx, uint8_t& lastInLag, const HCL_Comm comm)
{
    int maxSubPort = 0;
    if (isScaleOutPort(nic, comm))
    {
        lagIdx     = getComm(comm).getCommConnectivity().getScaleoutSubPortIndex(nic);
        maxSubPort = getComm(comm).getCommConnectivity().getNumScaleOutPorts() - 1;
    }
    else
    {
        lagIdx     = getServerConnectivity().getSubPortIndex(nic, comm);
        maxSubPort = getServerConnectivity().getMaxSubPort(false);
    }
    lastInLag = (lagIdx == maxSubPort);
    LOG_HCL_DEBUG(HCL,
                  "nic={}, comm={}, lagIdx={}, maxSubPort={}, lastInLag={}",
                  nic,
                  comm,
                  lagIdx,
                  maxSubPort,
                  lastInLag);
}

uint8_t HclDeviceGaudi3::getPeerNic(const HCL_Rank rank, const HCL_Comm comm, const uint8_t port)
{
    const HclConfigType configType = (HclConfigType)GCFG_BOX_TYPE_ID.value();
    if (getComm(comm).isRankInsideScaleupGroup(rank))  // scaleup port
    {
        if (configType == LOOPBACK)
        {
            return port;
        }
        else
        {
            return getServerConnectivity().getPeerPort(port, comm);
        }
    }
    else  // scaleout rank
    {
        // Handle remote peers / non peers, non-peers can have different scaleout ports
        const nics_mask_t myScaleOutPorts = getComm(comm).getCommConnectivity().getScaleOutPorts();
        const unsigned    remoteDevice = getComm(comm).m_remoteDevices[rank]->header.hwModuleID;  // Find target device
        const nics_mask_t remoteScaleoutPorts =
            getServerConnectivityGaudi3().getRemoteScaleOutPorts(remoteDevice,
                                                                 comm);  // get the remote scaleout ports list
        LOG_HCL_TRACE(HCL,
                      "rank={}, port={}, myScaleOutPorts={}, remoteDevice={}, remoteScaleoutPorts={}",
                      rank,
                      port,
                      myScaleOutPorts.to_str(),
                      remoteDevice,
                      remoteScaleoutPorts.to_str());
        for (auto myScaleOutPort : myScaleOutPorts)
        {
            if (port == myScaleOutPort)  // Find the required port in our device scaleout ports list
            {
                const unsigned subPortIndex = getServerConnectivity().getSubPortIndex(port, comm);
                VERIFY(subPortIndex < remoteScaleoutPorts.count(),
                       "subPortIndex={} out of range for remote rank={}, port={}, remoteDevice={}, "
                       "remoteScaleoutPorts.size={}",
                       subPortIndex,
                       rank,
                       port,
                       remoteDevice,
                       remoteScaleoutPorts.count());
                const unsigned peerPort = remoteScaleoutPorts(subPortIndex);
                // We assume same disabled port masks for current and remote devices
                const uint8_t peerNic = configType == LOOPBACK ? port : peerPort;
                LOG_HCL_TRACE(HCL,
                              "rank={}, port={}, remoteDevice={}, subPortIndex={}, peerPort={}, peerNic={}",
                              rank,
                              port,
                              remoteDevice,
                              subPortIndex,
                              peerPort,
                              peerNic);
                return peerNic;
            }
        }
        VERIFY(false, "Didn't find any scaleout ports for port={}, remoteRank={}, comm={}", port, rank, comm);
    }
}

void HclDeviceGaudi3::setEdmaEngineGroupSizes()
{
    edmaEngineGroupSizes[0] = m_scalManager.getNumberOfEdmaEngines(0);
    LOG_HCL_TRACE(HCL, "EDMA group0 has {} engines", edmaEngineGroupSizes[0]);
}

void HclDeviceGaudi3::openWQs()
{
    VERIFY(m_hal);

    for (auto nic : m_hclNic.mask)
    {
        // Hybrid ports can be used as both SU and SO
        // Since WQs are only opened once (not per comm) we must assume that at some point in time
        // a hybrid port will be possible used for SO, so this QP should be allocated.
        const uint32_t max_qps =
            isScaleOutPort(nic /*, HCL_Comm comm*/) ? m_hal->getMaxQpPerExternalNic() : m_hal->getMaxQpPerInternalNic();

        m_hclNic[nic] = allocateNic(nic, max_qps + 1);
    }

    g_ibv.create_fifos(m_scalManager.getScalHandle());

    for (auto nic : m_hclNic.mask)
    {
        m_hclNic[nic]->init();
    }
}

void HclDeviceGaudi3::deleteMigrationQPs(const HCL_Comm comm)
{
    getComm(comm).m_migrationQpManager.releaseQpsResource(*this, comm, getComm(comm).getOuterRanksExclusive());
}

void HclDeviceGaudi3::setMigrationQPsRTR(const HCL_Comm comm)
{
    QPManagerHints hints(comm, 0, 0, 0, 0, 0);
    QPManagerHints migratedQPhints(comm, 0, 0, 0, 0, 0);

    for (unsigned nic = 0; nic < getComm(comm).m_migrationQpManager.getNumMigrationNics(); nic++)
    {
        migratedQPhints.m_nic = nic;
        for (unsigned rank = 0; rank < getComm(comm).getCommSize(); rank++)
        {
            hints.m_remoteRank           = rank;
            migratedQPhints.m_remoteRank = rank;
            for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
            {
                hints.m_qpSet           = qpSet;
                migratedQPhints.m_qpSet = qpSet;
                for (unsigned qpi = 0; qpi < getComm(comm).m_migrationQpManager.getMaxQpsPerConnection(); qpi++)
                {
                    hints.m_qpi           = qpi;
                    migratedQPhints.m_qpi = qpi;
                    const unsigned qpn    = getComm(comm).m_migrationQpManager.getQPn(migratedQPhints);
                    if (qpn != INVALID_QP)
                    {
                        const unsigned         oldNic = getComm(comm).m_migrationQpManager.getOldNic(migratedQPhints);
                        const unsigned         newNic = getComm(comm).m_migrationQpManager.getNewNic(migratedQPhints);
                        const GaudiNicAddress& srcNewNic =
                            getComm(comm).m_rankInfo.device.gaudiNicAddresses.nics[newNic];
                        unsigned migratedQpn = getComm(comm).m_qpManagers[oldNic]->getQPn(hints);

                        auto offs = getNicToQpOffset(oldNic);
                        migratedQpn += offs;

                        const uint16_t         peerNic = getPeerNic(rank, comm, newNic);
                        const GaudiNicAddress& remoteNicAddress =
                            getComm(comm).m_remoteDevices[rank]->device.gaudiNicAddresses.nics[peerNic];
                        const NicQPs& remoteQPs = getComm(comm).m_remoteDevices[rank]->remoteInfo.gaudiNicQPs[peerNic];
                        LOG_HCL_DEBUG(HCL,
                                      "comm {} rank {} nic {} qp set {} qpi {} oldNic {} newNic {} "
                                      "QPN {} mQPN {} peerNic {}, dst qpn {}",
                                      comm,
                                      rank,
                                      nic,
                                      qpSet,
                                      qpi,
                                      oldNic,
                                      newNic,
                                      qpn,
                                      migratedQpn,
                                      peerNic,
                                      remoteQPs.qp[qpSet][getComm(comm).m_qpManagers.at(nic)->getDestQPi(qpi)]);

                        g_ibv.set_migration_qp_rtr(
                            comm,
                            newNic,
                            qpn,
                            oldNic,
                            migratedQpn,
                            srcNewNic.ip,
                            srcNewNic.mac.u64,
                            remoteNicAddress.ip,
                            remoteNicAddress.mac.u64,
                            remoteQPs.qp[qpSet][getComm(comm).m_qpManagers.at(nic)->getDestQPi(qpi)]);
                    }
                }
            }
        }
    }
}

void HclDeviceGaudi3::setMigrationQPsRTS(const HCL_Comm comm)
{
    QPManagerHints hints(comm, 0, 0, 0, 0, 0);
    QPManagerHints migratedQPhints(comm, 0, 0, 0, 0, 0);

    for (unsigned nic = 0; nic < getComm(comm).m_migrationQpManager.getNumMigrationNics(); nic++)
    {
        migratedQPhints.m_nic = nic;
        for (unsigned rank = 0; rank < getComm(comm).getCommSize(); rank++)
        {
            hints.m_remoteRank           = rank;
            migratedQPhints.m_remoteRank = rank;
            for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
            {
                hints.m_qpSet           = qpSet;
                migratedQPhints.m_qpSet = qpSet;
                for (unsigned qpi = 0; qpi < getComm(comm).m_migrationQpManager.getMaxQpsPerConnection(); qpi++)
                {
                    hints.m_qpi           = qpi;
                    migratedQPhints.m_qpi = qpi;
                    const unsigned qpn    = getComm(comm).m_migrationQpManager.getQPn(migratedQPhints);
                    if (qpn != INVALID_QP)
                    {
                        const unsigned oldNic      = getComm(comm).m_migrationQpManager.getOldNic(migratedQPhints);
                        const unsigned newNic      = getComm(comm).m_migrationQpManager.getNewNic(migratedQPhints);
                        unsigned       migratedQpn = getComm(comm).m_qpManagers[oldNic]->getQPn(hints);

                        auto offs = getNicToQpOffset(oldNic);
                        migratedQpn += offs;

                        g_ibv.set_migration_qp_rts(comm, newNic, qpn, oldNic, migratedQpn);
                    }
                }
            }
        }
    }
}

void HclDeviceGaudi3::migrateQPs(const HCL_Comm comm)
{
    QPManagerHints migratedQPhints(comm, 0, 0, 0, 0, 0);

    for (unsigned nic = 0; nic < getComm(comm).m_migrationQpManager.getNumMigrationNics(); nic++)
    {
        migratedQPhints.m_nic = nic;
        for (unsigned rank = 0; rank < getComm(comm).getCommSize(); rank++)
        {
            migratedQPhints.m_remoteRank = rank;
            for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
            {
                migratedQPhints.m_qpSet = qpSet;
                for (unsigned qpi = 0; qpi < getComm(comm).m_migrationQpManager.getMaxQpsPerConnection(); qpi++)
                {
                    migratedQPhints.m_qpi = qpi;
                    const unsigned qpn    = getComm(comm).m_migrationQpManager.getQPn(migratedQPhints);
                    if (qpn != INVALID_QP)
                    {
                        const unsigned newNic = getComm(comm).m_migrationQpManager.getNewNic(migratedQPhints);

                        g_ibv.migrate_qp(comm, newNic, qpn);
                    }
                }
            }
        }
    }
}

void HclDeviceGaudi3::reportCommNicStatus(const uint16_t port, const bool up)
{
    // if comm init rank in progress, wait until it is completed (new communicator added to
    // m_faultToleranceScaleoutComms )
    locker_t locker(hccl_ctx.comm_init_lock());

    HLFT_INF("port={} is {}", port, up ? "up" : "shutdown");
    m_failedScaleOutPortsMask[port] = !up;

    for (auto& comm : hccl_ctx.comms())
    {
        hccl_communicator&      hcclCommunicator(*(comm.second.get()));
        HclDynamicCommunicator& dynamicComm(*(hcclCommunicator.getDynamicComm()));
        const HCL_Comm          commId = dynamicComm;
        if (m_faultToleranceScaleoutComms.find(commId) == m_faultToleranceScaleoutComms.end())
        {
            HLFT_TRC("Skipping scaleup only comm {}, myRank={}", commId, dynamicComm.getMyRank());
            continue;
        }
        HLFT_DBG("Need to handle scaleout comm {}, myRank={}", commId, dynamicComm.getMyRank());
        const NicState nicState = {dynamicComm.getMyRank(), port, up};
        hcclCommunicator.getCoordClient()->sendNicStateChange(nicState);
    }
}

void HclDeviceGaudi3::updateNicState(const uint32_t nic, const NicLkdEventsEnum event, const bool atInit)
{
    IHclDevice::updateNicState(nic, event, atInit);
    if (atInit)
    {
        return;
    }
    HLFT_INF("nic={}, event={}, atInit={}", nic, (unsigned)event, atInit);
    m_nicsEventsHandler->nicStatusChange(nic, event);
}

const eIbvNicPhysicalState HclDeviceGaudi3::getNicPhysicalState(const uint32_t nic)
{
    if (!(m_hclNic.mask[nic]))
    {
        return eIbvNicPhysicalState::Undefined;
    }
    return g_ibv.get_nic_phys_state(nic);
}

extern std::unordered_map<HCL_Comm, spHcclCoordinatorClient> g_hcclCordClient;

void HclDeviceGaudi3::updateMigrationQpsToRts(const CommIds& commIds)
{
    HLFT_COMM_HDR_INF("move to RTR", commIds);
    setMigrationQPsRTR(commIds.commId);
    HLFT_COMM_HDR_INF("mQP moved to RTR", commIds);
    setMigrationQPsRTS(commIds.commId);
    HLFT_COMM_HDR_INF("moved mQP to RTS", commIds);
    g_hcclCordClient[commIds.commId]->rendezvous();
    HLFT_COMM_HDR_INF("rendezvous done", commIds);
    migrateQPs(commIds.commId);
    HLFT_COMM_HDR_INF("QP migration done", commIds);
}

void HclDeviceGaudi3::faultToleranceCommInit(const HCL_Comm comm)
{
    LOG_HCL_TRACE(HCL, "---comm {}", comm);
    HclDynamicCommunicator& dynamicComm = getComm(comm);
    if (dynamicComm.isCommunicatorMultiScaleupGroup())
    {
        LOG_HCL_TRACE(HCL, "Adding scaleout comm {}", comm);
        m_faultToleranceScaleoutComms.insert(comm);
    }
}

hcclResult_t HclDeviceGaudi3::destroyComm(HCL_Comm comm, bool force)
{
    LOG_HCL_TRACE(HCL, "---comm {}", comm);
    HclDeviceGen2Arch::destroyComm(comm, force);
    {
        LOG_HCL_TRACE(HCL, "Removing comm {}", comm);
        m_faultToleranceScaleoutComms.erase(comm);
        s_faultToleranceGroupScaleoutComms.erase(comm);
    }

    return hcclSuccess;
}

// Group API can continue (return true from here) if at least one scaleout comm counters are less than the targets
// If at least one is less, we should unblock the group API's
// We assume in the group we advance the counters for all the scaleout comms at the same time
// Note this function can run on multiple aggregator threads
//
bool HclDeviceGaudi3::checkFaultToleranceGroupEndAllCommsUntil() const
{
    if (s_faultToleranceGroupScaleoutComms.size() == 0)
    {
        HLFT_TRC("No scaleout comms, return true");
        return true;
    }

    HLFT_TRC("Checking group scaleout comms, size={}", s_faultToleranceGroupScaleoutComms.size());
    bool     groupApiCanContinue = false;
    unsigned commsIgnored        = 0;
    for (const HCL_Comm commId : s_faultToleranceGroupScaleoutComms)
    {
        const HclDynamicCommunicator& dynamicComm = getComm(commId);
        const hccl_communicator&      hcclCommunicator(*(hccl_ctx.communicator(dynamicComm.getCommHandle())));

        const TargetCountersCheckResult targetCountersCheckResult =
            hcclCommunicator.checkFaultToleranceStopCommSendRecvApiUntil();

        if (targetCountersCheckResult == TargetCountersCheckResult::FT_TARGET_COUNTERS_CHECK_RESULT_IGNORE)
        {
            commsIgnored++;
            continue;  // Ignore this comm
        }
        else
        {
            const bool thisCommShouldContinue =
                (targetCountersCheckResult == TargetCountersCheckResult::FT_TARGET_COUNTERS_CHECK_RESULT_LESS_THAN);
            groupApiCanContinue |= thisCommShouldContinue;
            HLFT_TRC("commId={}, thisCommShouldContinue={}, groupApiCanContinue={}, commsIgnored={}",
                     commId,
                     thisCommShouldContinue,
                     groupApiCanContinue,
                     commsIgnored);
            if (thisCommShouldContinue)  // If at least one comm should continue, we should continue
            {
                HLFT_TRC("commId={}, return true", commId);
                return true;
            }
        }
    }
    if (commsIgnored == s_faultToleranceGroupScaleoutComms.size())
    {
        HLFT_TRC("All comms ignored, return true");
        return true;  // All comms ignored, we can continue
    }
    HLFT_TRC("At least one comm has reached target, commsIgnored={}, return false", commsIgnored);
    return groupApiCanContinue;  // Group API should block (return false)
}

void HclDeviceGaudi3::faultToleranceContinueGroupEndApiUntil()
{
    HLFT_TRC("Stop API check");

    std::unique_lock<std::mutex> lk(m_faultsStopGroupEndApiMutex);
    HLFT_DBG("Before CV wait, s_faultToleranceGroupScaleoutComms.size={}", s_faultToleranceGroupScaleoutComms.size());

    m_faultsStopGroupEndApiCv.wait(lk, [this] {
        return (checkFaultToleranceGroupEndAllCommsUntil());
    }); /* The condition inside wait() must be true in order for unblock.
           Unblock if any of the current scaleout comms group s/r counters did not reach yet their target.
           Assumes all comms in the group advance with same API's.
           If any comm target is 0 we will block forever */

    HLFT_INF("After CV wait, User API thread is unblocked");
}

void HclDeviceGaudi3::handleFaultToleranceGroupEndApi()
{
    LOG_HCL_TRACE(HCL,
                  "Started, s_faultToleranceGroupScaleoutComms.size={}",
                  s_faultToleranceGroupScaleoutComms.size());

    if (g_faultsCheckStopApi.load())
    {
        faultToleranceContinueGroupEndApiUntil();  // If no scaleout comms, will not block
    }

    // Increment s/r counters for entire group of scaleout comms
    for (const auto comm : s_faultToleranceGroupScaleoutComms)  // Iterate over all group scaleout comms and increment
                                                                // their S/R group API counters
    {
        HclDynamicCommunicator& dynamicComm = getComm(comm);
        dynamicComm.updateApiSendRecvCounters();
        LOG_HCL_TRACE(HCL, "Increase group s/r counters for comm={}", comm);
    }
}

void HclDeviceGaudi3::faultToleranceNotifyGroupApis()
{
    HLFT_API_INF("--- Performing notify group APIs");
    // Notify user API thread to resume
    {
        std::lock_guard<std::mutex> lk(m_faultsStopGroupEndApiMutex);  // Remove ???
    }
    m_faultsStopGroupEndApiCv.notify_all();
    HLFT_DBG("After notify");
}

void HclDeviceGaudi3::clearScaleoutCommsCurrentGroup()
{
    LOG_HCL_DEBUG(HCL, "Clearing scaleout comms current group");
    s_faultToleranceGroupScaleoutComms.clear();
}

void HclDeviceGaudi3::addScaleoutCommsCurrentGroup(const HCL_Comm hclCommId)
{
    LOG_HCL_DEBUG(HCL, "Adding scaleout comm {} to current group", hclCommId);
    s_faultToleranceGroupScaleoutComms.insert(hclCommId);
}

void HclDeviceGaudi3::destroy()
{
    for (auto& report : m_delayedReports)
    {
        report.cancel();
    }

    HclDeviceGen2Arch::destroy();
}

void HclDeviceGaudi3::setQpManagersForComm(const HCL_Comm comm, const size_t commSize)
{
    std::shared_ptr<QPManagerGaudi3ScaleUp>  qpManagerScaleUp  = std::make_shared<QPManagerGaudi3ScaleUp>(*this);
    std::shared_ptr<QPManagerGaudi3ScaleOut> qpManagerScaleOut = std::make_shared<QPManagerGaudi3ScaleOut>(*this);

    if (!m_scaleoutProvider || !m_scaleoutProvider->isHostNic())
    {
        qpManagerScaleOut->resizeDBPerComm(commSize);
    }

    for (unsigned nic = 0; nic < MAX_NICS_GEN2ARCH; nic++)
    {
        if (isScaleOutPort(nic /*, HCL_Comm comm*/))
        {
            getComm(comm).m_qpManagers.at(nic) = qpManagerScaleOut;
        }
        else
        {
            getComm(comm).m_qpManagers.at(nic) = qpManagerScaleUp;
        }
    }
}

uint32_t HclDeviceGaudi3::getCollectiveQpi(const HCL_CollectiveOp collectiveOp, const bool isSend)
{
    switch (collectiveOp)
    {
        case eHCLReduceScatter:
            return isSend ? G3::QP_e::QPE_RS_SEND : G3::QP_e::QPE_RS_RECV;
            break;
        case eHCLAllGather:
            return isSend ? G3::QP_e::QPE_AG_SEND : G3::QP_e::QPE_AG_RECV;
            break;
        case eHCLAll2All:
            return isSend ? G3::QP_e::QPE_A2A_SEND : G3::QP_e::QPE_A2A_RECV;
            break;
        default:
            VERIFY(false, "invalid op({})", collectiveOp);
    }

    VERIFY(false, "unreachable code");
    return 0;
}
