#include "platform/gaudi2/hcl_device.h"

#include <array>    // for array
#include <cstdint>  // for uint32_t
#include <memory>   // for __share...
#include <vector>   // for vector

#include "platform/gen2_arch_common/hcl_device_config.h"  // for HclDeviceConfig
#include "hcl_device.h"
#include "hcl_dynamic_communicator.h"                         // for HclDyna...
#include "hcl_global_conf.h"                                  // for GCFG_BO...
#include "interfaces/hcl_remote_device.h"                     // for HclRemo...
#include "hcl_types.h"                                        // for HclConf...
#include "hcl_utils.h"                                        // for LOG_HCL...
#include "infra/scal/gaudi2/scal_manager.h"                   // for Gaudi2S...
#include "interfaces/hcl_unique_sorted_vector.h"              // for UniqueS...
#include "platform/gaudi2/commands/hcl_commands.h"            // for HclComm...
#include "interfaces/hcl_hal.h"                               // for HalPtr
#include "platform/gen2_arch_common/commands/hcl_commands.h"  // for HclComm...
#include "platform/gen2_arch_common/eq_handler.h"             // for IEventQ...
#include "hcl_log_manager.h"                                  // for LOG_ERR
#include "hcl_nic.h"                                          // for HclNic
#include "platform/gaudi2/simb_pool_container_allocator.h"    // for SimbP...
#include "platform/gen2_arch_common/scaleout_provider.h"      // for ScaleoutProvider
#include "platform/gen2_arch_common/hcl_device_controller.h"  //
#include "platform/gaudi2/hcl_device_controller.h"            // for HclDeviceControllerGaudi2
#include "hcl_log_manager.h"                                  // for LOG_ERR
#include "hccl_communicator.h"
#include "hccl_helpers.h"
#include "hccl_internal_defs.h"
#include "hccl_types.h"
#include "ibverbs/hcl_ibverbs.h"
#include "platform/gen2_arch_common/server_def.h"                // for Gen2ArchServerDef
#include "platform/gaudi2/signals/calculator.h"                  // for SignalsCalculatorGaudi2
#include "platform/gen2_arch_common/nics_events_handler_impl.h"  // for NicsEventHandler

#define IS_RS_QP(stream) ((stream & 1) != 1)

/* This is a tests-only constructor, so the nic array in a few lines is allowed... :-\ */
HclDeviceGaudi2::HclDeviceGaudi2(HclDeviceControllerGen2Arch& controller,
                                 HclDeviceConfig&             deviceConfig,
                                 Gen2ArchServerDef&           serverDef)
: HclDeviceGen2Arch(true, controller, deviceConfig, serverDef)
{
    registerOpenQpCallback(LOOPBACK, [&](HCL_Comm comm) { return openQpsLoopback(comm); });
    registerOpenQpCallback(HLS2, [&](HCL_Comm comm) { return openQpsHLS(comm); });
    registerOpenQpCallback(HL288, [&](HCL_Comm comm) { return openQpsHLS(comm); });
    setHal(serverDef.getHalSharedPtr());
    LOG_HCL_TRACE(HCL, "Test ctor, deviceType={}", deviceConfig.getDeviceTypeStr());
}

// Runtime ctor
HclDeviceGaudi2::HclDeviceGaudi2(HclDeviceControllerGen2Arch& controller,
                                 HclDeviceConfig&             deviceConfig,
                                 [[maybe_unused]] hcl::HalPtr halShared,
                                 Gen2ArchServerDef&           serverDef)
: HclDeviceGen2Arch(controller, deviceConfig, serverDef)
{
    const HclConfigType configType = (HclConfigType)GCFG_BOX_TYPE_ID.value();
    if ((configType == HLS2) || (configType == LOOPBACK))
    {
        m_boxConfigType = HLS2;
    }
    else if (configType == HL288)
    {
        m_boxConfigType = HL288;
    }
    else
    {
        VERIFY(false, "Invalid server type {} for G2 device", configType);
    }
    LOG_HCL_INFO(HCL, "Set server type to {}", m_boxConfigType);

    m_scalManager.getHBMAddressRange(m_allocationRangeStart, m_allocationRangeEnd);
    setHal(serverDef.getHalSharedPtr());
    // The scaleout mode is set according also to if all scaleout ports are disabled by LKD/HCL or not. This is
    // regardless of communicator setup.
    setScaleoutMode(getServerConnectivity().getNumScaleOutPortsGlbl());
    createOfiPlugin();
    m_sibContainerManager = std::make_unique<SimbPoolContainerAllocatorGaudi2>(m_hal->getMaxArchStreams());
    m_sibContainerManager->init();

    m_contextManager = std::make_unique<ContextManager>(m_scalManager.getNicsScaleUpEngines(), *this);
    m_contextManager->createCollectiveContexts(controller.getGen2ArchCommands() /*, HCL_Comm comm */);
    registerOpenQpCallback(LOOPBACK, [&](HCL_Comm comm) { return openQpsLoopback(comm); });
    registerOpenQpCallback(HLS2, [&](HCL_Comm comm) { return openQpsHLS(comm); });
    registerOpenQpCallback(HL288, [&](HCL_Comm comm) { return openQpsHLS(comm); });

    updateDisabledPorts();
    initNicsMask();
    openWQs();

    m_nicsEventsHandler = std::make_unique<NicsEventHandler>(*this);
    m_eqHandler         = new IEventQueueHandler(*m_nicsEventsHandler);
    m_eqHandler->startThread(this);
    m_scaleoutProvider = ScaleoutProvider::createScaleOutProvider(this);
    setEdmaEngineGroupSizes();
    m_signalsCalculator = std::make_unique<SignalsCalculatorGaudi2>();
}

hlthunk_device_name HclDeviceGaudi2::getDeviceName()
{
    return HLTHUNK_DEVICE_GAUDI2;
}

uint8_t HclDeviceGaudi2::getPeerNic([[maybe_unused]] const HCL_Rank rank, const HCL_Comm comm, const uint8_t port)
{
    HclConfigType configType = (HclConfigType)GCFG_BOX_TYPE_ID.value();
    return configType == LOOPBACK ? port : getServerConnectivity().getPeerPort(port, comm);
}

unsigned HclDeviceGaudi2::getSenderWqeTableSize()
{
    // sendWqeTable size must be at least 4 * recvWqeTable size because of SW-109975. we set it to 8 because of
    // SW-211841.
    constexpr int SEND_RECV_WQE_FACTOR = 8;
    return getReceiverWqeTableSize() * SEND_RECV_WQE_FACTOR;
}

unsigned HclDeviceGaudi2::getReceiverWqeTableSize()
{
    return m_cgSize;
}

void HclDeviceGaudi2::addQPsToQPManagerDB(const HCL_Comm   comm,
                                          const HCL_Rank   remoteRank,
                                          const QpsVector& qps,
                                          const size_t     nic)
{
    const uint8_t qpSets = getNumQpSets(isScaleOutPort(nic, comm), comm, remoteRank);

    VERIFY(qps.size() == m_hal->getMaxQPsPerNic() * qpSets,
           "Each connection should hold {} QPs but opened {} QPs: comm {} remoteRank {} nic {}",
           m_hal->getMaxQPsPerNic() * qpSets,
           qps.size(),
           comm,
           remoteRank,
           nic);

    m_contextManager->registerEarc(comm, nic);

    const QPManagerHints hints(comm, remoteRank, nic);

    getComm(comm).m_qpManagers.at(nic)->addQPsToQPManagerDB(hints, qps);
}

bool HclDeviceGaudi2::isSender(unsigned _qpi)
{
    return ((_qpi == G2::QP_e::QPE_RS_SEND) || (_qpi == G2::QP_e::QPE_AG_SEND));
}

uint32_t
HclDeviceGaudi2::getQpi(HCL_Comm comm, uint8_t nic, HCL_Rank remoteRank, uint32_t qpn, [[maybe_unused]] uint8_t qpSet)
{
    const QPManagerHints hints(comm, remoteRank, nic, INVALID_QP, qpn);

    return getComm(comm).m_qpManagers.at(nic)->getQPi(hints);
}

hcclResult_t HclDeviceGaudi2::openQpsLoopback(HCL_Comm comm)
{
    HclConfigType configType = (HclConfigType)GCFG_BOX_TYPE_ID.value();
    if (configType != LOOPBACK)
    {
        LOG_HCL_ERR(HCL, "Invalid config type ({}), expecting LOOPBACK ({})", configType, LOOPBACK);
        return hcclInvalidArgument;
    }

    LOG_HCL_HEADER(HCL);

    initRemoteNicsLoopback(comm);

    // loop over all the nics, 3 per rank
    for (HCL_Rank rank = 0; rank < getCommSize(comm); rank++)
    {
        if (rank == getMyRank(comm) ||
            (rank >= GCFG_LOOPBACK_SCALEUP_GROUP_SIZE.value() && !getComm(comm).isPeer(rank)))
            continue;
        for (uint16_t index = 0; index < COMPACT_RANK_INFO_NICS; index++)
        {
            uint32_t nic = getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs.qp[index].nic;
            if ((!m_hclNic.mask[nic])) continue;

            QpsVector qps;

            // allocate max QPs per nic
            for (unsigned qpSet = 0; qpSet < getNumQpSets(isScaleOutPort(nic), comm, rank); qpSet++)
            {
                for (unsigned qpi = 0; qpi < m_hal->getMaxQPsPerNic(); qpi++)
                {
                    qps.push_back(allocateQp(nic, rank, comm, qpi, qpSet));
                }
            }
            addQPsToQPManagerDB(comm, rank, qps, nic);
        }
    }

    return hcclSuccess;
}

hcclResult_t HclDeviceGaudi2::openQpsHlsScaleUp(HCL_Comm comm)
{
    UniqueSortedVector innerRanks;
    getInnerRanks(comm, innerRanks);
    LOG_HCL_INFO(HCL, "Open scale-up QPs");
    return openQpToRemoteRanks(comm, innerRanks);
}

hcclResult_t HclDeviceGaudi2::openQpsScaleOut(HCL_Comm comm, const UniqueSortedVector& outerRanks)
{
    LOG_HCL_INFO(HCL, "Open scale-out QPs, outerRanks={}", outerRanks);
    return openQpToRemoteRanks(comm, outerRanks);
}

hcclResult_t HclDeviceGaudi2::openQpToRemoteRanks(const HCL_Comm comm, const UniqueSortedVector& ranks)
{
    // in null-submit mode don't open QPs
    if (GCFG_HCL_NULL_SUBMIT.value())
    {
        return hcclSuccess;
    }

    for (auto& rank : ranks)
    {
        if (rank == getMyRank(comm)) continue;

        openQpToSingleRank(comm, rank);
    }

    return hcclSuccess;
}

void HclDeviceGaudi2::openQpToSingleRank(const HCL_Comm comm, const HCL_Rank remoteRank)
{
    LOG_HCL_TRACE(HCL, "remoteRank={}", remoteRank);
    if (m_QpConnectionExistsForRank[comm].count(remoteRank))
    {
        return;
    }

    for (auto nic : getActiveNics(getMyRank(comm), remoteRank, 1, comm))
    {
        QpsVector qps;
        uint8_t   qpSets = getNumQpSets(isScaleOutPort(nic, comm), comm, remoteRank);
        bool      isPeer = !isScaleOutPort(nic, comm) || getComm(comm).isPeer(remoteRank);
        for (uint8_t qpSet = 0; qpSet < qpSets; qpSet++)
        {
            for (unsigned qpi = 0; qpi < m_hal->getMaxQPsPerNic(); qpi++)
            {
                unsigned qp = (unsigned)INVALID_QP;
                if (isPeer || IS_RS_QP(qpi) || !(GCFG_HCL_REDUCE_NON_PEER_QPS.value()))
                {
                    qp = allocateQp(nic, remoteRank, comm, qpi, qpSet);
                }
                qps.push_back(qp);
                LOG_HCL_DEBUG(HCL,
                              "nic {} remoteRank {} comm {} qpSet {} qpi {} qp {}",
                              nic,
                              remoteRank,
                              comm,
                              qpSet,
                              qpi,
                              qp);
            }
        }
        LOG_HCL_DEBUG(HCL, "registering qps for nic {}", nic);
        addQPsToQPManagerDB(comm, remoteRank, qps, nic);
    }
    updateRankHasQp(comm, remoteRank);
}

void HclDeviceGaudi2::updateDisabledPorts()
{
    const uint64_t disabledPortsMap = ~(getServerConnectivity().getEnabledPortsMask(/*HCL_Comm comm*/));
    m_deviceConfig.updateDisabledPorts(disabledPortsMap);
}

spHclNic HclDeviceGaudi2::allocateNic(uint32_t nic, uint32_t max_qps)
{
    return std::make_shared<Gaudi2Nic>(this,
                                       nic,
                                       max_qps,
                                       getServerConnectivity().getBackpressureOffset(nic /*, HCL_Comm comm*/));
}

ContextManager& HclDeviceGaudi2::getContextManager()
{
    return *m_contextManager;
}

hcclResult_t HclDeviceGaudi2::connectCommQps(HCL_Comm comm)
{
    LOG_HCL_HEADER(HCL);

    LOG_HCL_INFO(HCL, "Update scale-up QPs");
    for (auto& rank : getComm(comm).getInnerRanksExclusive())
    {
        connectRankQps(comm, rank);
    }

    LOG_HCL_INFO(HCL, "Update scale-out connections");
    m_scaleoutProvider->verifyConnections(comm);

    return hcclSuccess;
}

void HclDeviceGaudi2::setEdmaEngineGroupSizes()
{
    edmaEngineGroupSizes[0] = m_scalManager.getNumberOfEdmaEngines(0);
    LOG_HCL_TRACE(HCL, "EDMA group0 has {} engines", edmaEngineGroupSizes[0]);
}

void HclDeviceGaudi2::setQpManagersForComm(const HCL_Comm comm, const size_t commSize)
{
    std::shared_ptr<QPManagerGaudi2ScaleOut> scaleOutQpManager = std::make_shared<QPManagerGaudi2ScaleOut>(*this);
    std::shared_ptr<QPManagerGaudi2ScaleUp>  scaleupQpManager  = std::make_shared<QPManagerGaudi2ScaleUp>(*this);

    if (!m_scaleoutProvider || !m_scaleoutProvider->isHostNic())
    {
        scaleOutQpManager->resizeDBPerComm(commSize);
    }

    for (unsigned nic = 0; nic < MAX_NICS_GEN2ARCH; nic++)
    {
        if (isScaleOutPort(nic /*, HCL_Comm comm*/))
        {
            getComm(comm).m_qpManagers.at(nic) = scaleOutQpManager;
        }
        else
        {
            getComm(comm).m_qpManagers.at(nic) = scaleupQpManager;
        }
    }
}

uint32_t HclDeviceGaudi2::getCollectiveQpi(const HCL_CollectiveOp collectiveOp, const bool isSend)
{
    switch (collectiveOp)
    {
        case eHCLReduceScatter:
            return isSend ? G2::QP_e::QPE_RS_SEND : G2::QP_e::QPE_RS_RECV;
            break;
        case eHCLAllGather:
            return isSend ? G2::QP_e::QPE_AG_SEND : G2::QP_e::QPE_AG_RECV;
            break;
        default:
            VERIFY(false, "invalid op({})", collectiveOp);
    }

    VERIFY(false, "unreachable code");
    return 0;
}
