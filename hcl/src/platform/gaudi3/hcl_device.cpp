#include "platform/gaudi3/hcl_device.h"

#include <memory>                      // for make_shared, make_unique
#include <utility>                     // for pair
#include <numeric>

#include "hcl_config.h"                // for HclDevi...
#include "hcl_dynamic_communicator.h"  // for HclDyna...
#include "hcl_global_conf.h"           // for GCFG_MA...
#include "hcl_types.h"                 // for HclConfigType
#include "hcl_utils.h"                                                // for VERIFY
#include "infra/scal/gaudi3/scal_manager.h"                           // for Gaudi3S...
#include "infra/scal/gen2_arch_common/scal_manager.h"                 // for Gen2Arc...
#include "platform/gaudi3/commands/hcl_commands.h"                    // for HclComm...
#include "platform/gaudi3/eq_handler.h"                               // for Gaudi3E...
#include "platform/gaudi3/hal.h"                                      // for Gaudi3Hal
#include "platform/gaudi3/hal_hls3pcie.h"                             // for Gaudi3Hls3PCieHal
#include "platform/gaudi3/qp_manager.h"                               // for QPManager
#include "platform/gen2_arch_common/intermediate_buffer_container.h"  // for IntermediateBufferContainer
#include "platform/gen2_arch_common/scaleout_provider.h"
#include "ibverbs/hcl_ibverbs.h"
#include "platform/gaudi3/port_mapping.h"           // for Gaudi3DevicePortMapping
#include "platform/gaudi3/port_mapping_hls3pcie.h"  // for HLS3PciePortMapping

/* This is a test-only constructor, so the nic array in a few lines is allowed... :-\ */
HclDeviceGaudi3::HclDeviceGaudi3(HclDeviceControllerGen2Arch& controller) : HclDeviceGen2Arch(controller)
{
    registerOpenQpCallback(LOOPBACK, [&](HCL_Comm comm) { return openQpsLoopback(comm); });
    registerOpenQpCallback(HLS3, [&](HCL_Comm comm) { return openQpsHLS(comm); });
    registerOpenQpCallback(HLS3PCIE, [&](HCL_Comm comm) { return openQpsHLS(comm); });
    setHal(std::make_shared<hcl::Gaudi3Hal>());
    m_portMapping       = std::make_unique<Gaudi3DevicePortMapping>(getFd(), getGaudi3Hal());
    m_qpManagerScaleUp  = std::make_unique<QPManagerScaleUp>(this);   // delayed ctor due to Hal
    m_qpManagerScaleOut = std::make_unique<QPManagerScaleOut>(this);  // delayed ctor due to Hal
}

/* tests only constructor */
HclDeviceGaudi3::HclDeviceGaudi3(HclDeviceControllerGen2Arch& controller, const int moduleId)
: HclDeviceGen2Arch(controller)
{
    registerOpenQpCallback(LOOPBACK, [&](HCL_Comm comm) { return openQpsLoopback(comm); });
    registerOpenQpCallback(HLS3, [&](HCL_Comm comm) { return openQpsHLS(comm); });
    registerOpenQpCallback(HLS3PCIE, [&](HCL_Comm comm) { return openQpsHLS(comm); });
    setHal(std::make_shared<hcl::Gaudi3Hal>());
    m_portMapping       = std::make_unique<Gaudi3DevicePortMapping>(getFd(), moduleId, getGaudi3Hal());
    m_qpManagerScaleUp  = std::make_unique<QPManagerScaleUp>(this);   // delayed ctor due to Hal
    m_qpManagerScaleOut = std::make_unique<QPManagerScaleOut>(this);  // delayed ctor due to Hal
}

HclDeviceGaudi3::HclDeviceGaudi3(HclDeviceControllerGen2Arch& controller, HclDeviceConfig& deviceConfig)
: HclDeviceGen2Arch(controller, deviceConfig)
{
    // Read box type and create server specific objects
    const HclConfigType configType = (HclConfigType)GCFG_BOX_TYPE_ID.value();
    if ((configType == HLS3) || (configType == LOOPBACK))
    {
        setHal(std::make_shared<hcl::Gaudi3Hal>());
        m_portMapping = std::make_unique<Gaudi3DevicePortMapping>(getFd(), m_portMappingConfig, getGaudi3Hal());
    }
    else if (configType == HLS3PCIE)
    {
        m_boxConfigType = HLS3PCIE;
        setHal(std::make_shared<hcl::Gaudi3Hls3PCieHal>(deviceConfig.getHwModuleId()));
        m_portMapping = std::make_unique<HLS3PciePortMapping>(getFd(), m_portMappingConfig, getGaudi3Hal());
    }
    else
    {
        VERIFY(false, "Invalid server type {} for G3 device", configType);
    }
    LOG_HCL_INFO(HCL, "Set server type to {}", m_boxConfigType);

    m_qpManagerScaleUp  = std::make_unique<QPManagerScaleUp>(this);   // delayed ctor due to Hal
    m_qpManagerScaleOut = std::make_unique<QPManagerScaleOut>(this);  // delayed ctor due to Hal

    m_scalManager.getHBMAddressRange(m_allocationRangeStart, m_allocationRangeEnd);
    registerOpenQpCallback(LOOPBACK, [&](HCL_Comm comm) { return openQpsLoopback(comm); });
    registerOpenQpCallback(HLS3, [&](HCL_Comm comm) { return openQpsHLS(comm); });
    registerOpenQpCallback(HLS3PCIE, [&](HCL_Comm comm) { return openQpsHLS(comm); });

    m_sibContainer = new hcl::IntermediateBufferContainer(m_deviceId, m_hal->getMaxStreams());

    if (GCFG_HCL_USE_IBVERBS.value())
    {
        g_ibv.init(this);
    }

    updateDisabledPorts();
    initNicsMask();
    openWQs();
    m_eqHandler = new Gaudi3EventQueueHandler();
    m_eqHandler->startThread(this);
    setScaleoutMode(m_portMapping->getNumScaleOutPorts(
        DEFAULT_SPOTLIGHT));  // DEFAULT_SPOTLIGHT can be used to determine scaleout mode
    createOfiPlugin();
    m_scaleoutProvider = ScaleoutProvider::createScaleOutProvider(this);
    setEdmaEngineGroupSizes();
}

hlthunk_device_name HclDeviceGaudi3::getDeviceName()
{
    return HLTHUNK_DEVICE_GAUDI3;
}

void HclDeviceGaudi3::registerQps(HCL_Comm comm, HCL_Rank remoteRank, const QpsVector& qps, int nic)
{
    if (remoteRank == HCL_INVALID_RANK || getComm(comm).isRankInsidePod(remoteRank))
    {
        return m_qpManagerScaleUp->registerQPs(comm, qps);
    }
    return m_qpManagerScaleOut->registerQPs(comm, qps, remoteRank);
}

uint32_t HclDeviceGaudi3::getDestQpi(unsigned qpi)
{
    switch (qpi)
    {
        case QPE_RS_RECV:
            return QPE_RS_SEND;
            break;
        case QPE_AG_RECV:
            return QPE_AG_SEND;
            break;
        case QPE_RS_SEND:
            return QPE_RS_RECV;
            break;
        case QPE_AG_SEND:
            return QPE_AG_RECV;
            break;
        case QPE_A2A_SEND:
            return QPE_A2A_RECV;
            break;
        case QPE_A2A_RECV:
            return QPE_A2A_SEND;
            break;
    }

    VERIFY(false, "unreachable code");

    return 0;
}

bool HclDeviceGaudi3::isSender(unsigned _qpi)
{
    return ((_qpi == QPE_RS_SEND) || (_qpi == QPE_AG_SEND) || (_qpi == QPE_A2A_SEND));
}

uint32_t HclDeviceGaudi3::getQpi(HCL_Comm comm, uint8_t nic, HCL_Rank remoteRank, uint32_t qpn, uint8_t qpSet)
{
    if (getComm(comm).isRankInsidePod(remoteRank))
    {
        return m_qpManagerScaleUp->getQpi(comm, nic, qpn);
    }
    return m_qpManagerScaleOut->getQpi(comm, nic, qpn, remoteRank);
}

uint32_t HclDeviceGaudi3::createCollectiveQp(bool isScaleOut)
{
    if (GCFG_HCL_USE_IBVERBS.value())
    {
        return g_ibv.create_collective_qp(isScaleOut);
    }

    uint32_t qpn = 0;
    struct hlthunk_nic_alloc_coll_conn_in isScaleOutIn = {.is_scale_out = isScaleOut};
    int rc = hlthunk_alloc_coll_conn(getFd(), &isScaleOutIn, &qpn);
    if (rc != 0)
    {
        g_status = hcclInternalError;
        LOG_HCL_CRITICAL(HCL,
                         "Failed to allocate QP, hlthunk_alloc_coll_conn(ScaleOut:{}) failed({}), Device may be out of "
                         "QPs, or network interfaces are not up",
                         isScaleOut,
                         rc);
        throw hcl::VerifyException("Failed to allocate QP");
    }

    return qpn;
}

void HclDeviceGaudi3::allocateQps(HCL_Comm comm, const bool isScaleOut, const HCL_Rank remoteRank, QpsVector& qpnArr)
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
        for (uint64_t i = 0; i < getHal()->getMaxQPsPerNic(); i++)
        {
            // for non-peers we only need to open the RS qps since they are used for send receive
            // for scale up and peers we open 6 qps (G3QP_e)
            // in null-submit mode don't open QPs
            if ((isScaleOut && !getComm(comm).isPeer(remoteRank) && !m_qpManagerScaleOut->isRsQp(i)) ||
                GCFG_HCL_NULL_SUBMIT.value())
            {
                qpnArr.push_back(0);
            }
            else
            {
                qpnArr.push_back(createCollectiveQp(isScaleOut));
            }

            LOG_HCL_DEBUG(HCL,
                          "Allocate QP, remoteRank({}){} qpSet: {}, QPi: {}, QPn: {}",
                          remoteRank,
                          remoteRank == getMyRank(comm) ? " Loopback connection, " : "",
                          qpSet,
                          i,
                          qpnArr[(qpSet * getHal()->getMaxQPsPerNic()) + i]);
        }
    }

    registerQps(comm, remoteRank, qpnArr);
}

inline uint32_t HclDeviceGaudi3::createQp(uint32_t nic, unsigned qpId, uint32_t coll_qpn)
{
    auto offs = getNicToQpOffset(nic);

    if (GCFG_HCL_USE_IBVERBS.value())
    {
        return g_ibv.create_qp(isSender(qpId), nic, coll_qpn + offs);
    }

    return coll_qpn + offs;
}

void HclDeviceGaudi3::openRankQps(HCL_Comm      comm,
                                  HCL_Rank      rank,
                                  nics_mask_t   nics,
                                  QpsVector&    qpnArr,
                                  const bool    isScaleOut)
{
    LOG_HCL_TRACE(HCL, "Processing rank={}", rank);

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
void HclDeviceGaudi3::openRankQpsLoopback(HCL_Comm comm, QpsVector& qpnArr)
{
    HCL_Rank myRank = getMyRank(comm);
    LOG_HCL_TRACE(HCL, "Processing rank={}", myRank);

    // initialize nic-index mapping
    initRemoteNicsLoopback(comm);

    uint8_t qpSets = getNumQpSets(false, comm, myRank);

    // loop over ranks/nics
    for (int rank = 0; rank < getCommSize(comm); rank++)
    {
        for (uint16_t index = 0; index < COMPACT_RANK_INFO_NICS; index++)
        {
            uint32_t nic = LOOPBACK_NIC_INDEX_INIT(index, rank);
            createNicQps(comm, rank, nic, qpnArr, qpSets);
        }
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
        uint8_t qpSetBase = getHal()->getMaxQPsPerNic() * qpSet;
        // allocate max QPs per nic
        for (unsigned i = 0; i < getHal()->getMaxQPsPerNic(); i++)
        {
            uint8_t qpnArrIndex = qpSetBase + i;
            if (qpnArr[qpnArrIndex] == 0) continue;
            uint32_t qpnWithOffset = createQp(nic, i, qpnArr[qpnArrIndex]);

            getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs[nic].qp[qpSet][i] = qpnWithOffset;
        }
    }
}

#define ACTIVE_NICS(rank) getActiveNics(getMyRank(comm), rank, 1, comm)

hcclResult_t HclDeviceGaudi3::openQpsHlsScaleUp(HCL_Comm comm)
{
    LOG_HCL_TRACE(HCL, "comm={}", comm);

    // comm is scale-out only, no need for internal QPs
    if (getComm(comm).getInnerRanksExclusive().size() == 0)
    {
        return hcclSuccess;
    }

    QpsVector qpnArr;
    allocateQps(comm, false, HCL_INVALID_RANK, qpnArr);

    // in null-submit mode don't open QPs
    if (unlikely(GCFG_HCL_NULL_SUBMIT.value()))
    {
        return hcclSuccess;
    }

    // loop over all scale up ranks
    for (auto& rank : getComm(comm).getInnerRanksExclusive())
    {
        openRankQps(comm, rank, ACTIVE_NICS(rank), qpnArr, false);
    }

    return hcclSuccess;
}

hcclResult_t HclDeviceGaudi3::openQpsHlsScaleOut(HCL_Comm comm, const UniqueSortedVector& outerRanks)
{
    LOG_HCL_TRACE(HCL, "comm={}, outerRanks={}", comm, outerRanks);

    // allocate scale-out QPs memory for communicator
    m_qpManagerScaleOut->allocateCommQPs(comm, getCommSize(comm));

    // loop over all outer ranks
    for (auto& rank : outerRanks)
    {
        QpsVector qpnArr;
        allocateQps(comm, true, rank, qpnArr);

        // in null-submit mode don't open QPs
        if (likely(!GCFG_HCL_NULL_SUBMIT.value()))
        {
            openRankQps(comm, rank, ACTIVE_NICS(rank), qpnArr, true);
        }
    }

    return hcclSuccess;
}

hcclResult_t HclDeviceGaudi3::openQpsLoopback(HCL_Comm comm)
{
    HclConfigType configType = (HclConfigType)GCFG_BOX_TYPE_ID.value();
    if (configType != LOOPBACK)
    {
        LOG_HCL_ERR(HCL, "Invalid config type ({}), expecting LOOPBACK ({})", configType, LOOPBACK);
        return hcclInvalidArgument;
    }

    LOG_HCL_TRACE(HCL, "");

    QpsVector qpnArr;
    allocateQps(comm, false, HCL_INVALID_RANK, qpnArr);
    openRankQpsLoopback(comm, qpnArr);

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

#define mmD0_NIC0_QM_SPECIAL_GLBL_SPARE_0        0xD009F60

uint32_t HclDeviceGaudi3::getBackpressureOffset(uint16_t nic)
{
    // TODO - Gaudi3 has a different bp mechanism than Gaudi2 (not only different reg).
    return mmD0_NIC0_QM_SPECIAL_GLBL_SPARE_0;  // TODO - use MASK_AGGREGATOR_P0_BITS_LSB
}

hcclResult_t HclDeviceGaudi3::updateQps(HCL_Comm comm)
{
    hcclResult_t rc;
    LOG_INFO(HCL, "Update scale-up QPs");
    for (auto& rank : getComm(comm).getInnerRanksInclusive())
    {
        rc = updateRankQps(comm, rank);
        VERIFY(rc == hcclSuccess, "updateQps failed rc={}", rc);
    }

    LOG_INFO(HCL, "Update scale-out connections");
    m_scaleoutProvider->verifyConnections(comm);

    getScalManager().configQps(comm, this);
    return rc;
}

void HclDeviceGaudi3::updateDisabledPorts()
{
    const uint64_t disabledPortsMap = ~(m_portMapping->getEnabledPortsMask());
    LOG_HCL_DEBUG(HCL, "disabledPortsMap={:024b}", disabledPortsMap);
    m_deviceConfig.updateDisabledPorts(
        disabledPortsMap,
        m_portMapping->getExternalPortsMask());  // In loopback, mask scaleout external ports always (they are different
                                                 // per device)
}

nics_mask_t HclDeviceGaudi3::getAllPorts(int deviceId, unsigned spotlightType)
{
    return m_portMapping->getAllPorts(deviceId, spotlightType);
};

void HclDeviceGaudi3::getLagInfo(int nic, uint8_t& lagIdx, uint8_t& lastInLag, unsigned spotlightType)
{
    lagIdx    = m_portMapping->getSubPortIndex(nic, spotlightType);
    lastInLag = (lagIdx == m_portMapping->getMaxSubPort(spotlightType));
}

bool HclDeviceGaudi3::isScaleOutPort(uint16_t port, unsigned spotlightType)
{
    return m_portMapping->isScaleoutPort(port, spotlightType);
}

uint64_t HclDeviceGaudi3::getEnabledPortsMask()
{
    return m_portMapping->getEnabledPortsMask();
}

uint8_t HclDeviceGaudi3::getPeerNic(HCL_Rank rank, HCL_Comm comm, uint8_t port)
{
    const HclConfigType configType    = (HclConfigType)GCFG_BOX_TYPE_ID.value();
    const unsigned      spotlightType = getComm(comm).getSpotlightType();
    if (getComm(comm).isRankInsidePod(rank))  // scaleup port
    {
        if (configType == LOOPBACK)
        {
            return port;
        }
        else
        {
            return m_portMapping->getPeerPort(port, spotlightType);
        }
    }
    else  // scaleout rank
    {
        // Handle remote peers / non peers, non-peers can have different scaleout ports
        nics_mask_t    myScaleOutPorts = m_portMapping->getScaleOutPorts();
        const unsigned remoteDevice = getComm(comm).m_remoteDevices[rank]->header.hwModuleID;  // Find target device
        nics_mask_t    remoteScaleoutPorts =
            m_portMapping->getRemoteScaleOutPorts(remoteDevice, spotlightType);  // get the remote scaleout ports list
        for (auto myScaleOutPort : myScaleOutPorts)
        {
            if (port == myScaleOutPort)  // Find the required port in our device scaleout ports list
            {
                const unsigned subPortIndex = m_portMapping->getSubPortIndex(port, spotlightType);
                VERIFY(subPortIndex < remoteScaleoutPorts.count(),
                       "subPortIndex={} out of range for remote rank={}, port={}, remoteDevice={}, "
                       "remoteScaleoutPorts.size={}",
                       subPortIndex,
                       rank,
                       port,
                       remoteDevice,
                       remoteScaleoutPorts.count());
                const unsigned peerPort = remoteScaleoutPorts(subPortIndex);  // TODO: Validate getRemoteSubPortIndex is same as subPortIndex
                // TODO: We assume same disabled port masks for current and remote devices
                // TODO: LOOPBACK may not work in case several loopback devices are used
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
        VERIFY(false,
               "Didn't find any scaleout ports for m_deviceId={}, port={}, remoteRank={}, comm={}",
               m_deviceId,
               port,
               rank,
               comm);
    }
}

void HclDeviceGaudi3::deleteCommConnections(HCL_Comm comm)
{
    UniqueSortedVector innerRanks = getComm(comm).getInnerRanksInclusive();
    LOG_INFO(HCL, "Close scale-up QPs");
    closeQps(comm, innerRanks);

    LOG_INFO(HCL, "Close scale-out connections");
    m_scaleoutProvider->closeConnections(comm);
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
        uint32_t max_qps =
            isScaleOutPort(nic, SCALEOUT_SPOTLIGHT) ? m_hal->getMaxQpPerExternalNic() : m_hal->getMaxQpPerInternalNic();

        m_hclNic[nic] = allocateNic(nic, max_qps + 1);
    }

    if (GCFG_HCL_USE_IBVERBS.value())
    {
        g_ibv.create_fifos(m_scalManager.getScalHandle());
    }
    else
    {
        int rc = scal_nics_db_fifos_init_and_alloc(m_scalManager.getScalHandle(), nullptr);
        VERIFY(rc == SCAL_SUCCESS, "scal_nics_db_fifos_init_and_alloc failed");
    }

    for (auto nic : m_hclNic.mask)
    {
        m_hclNic[nic]->init();
    }
}
