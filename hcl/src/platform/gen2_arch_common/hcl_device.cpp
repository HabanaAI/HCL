#include "platform/gen2_arch_common/hcl_device.h"

#include <pthread.h>                                                  // for pthread_self
#include <cstring>                                                    // for memset
#include <memory>                                                     // for __shared_ptr_access
#include "hcl_config.h"                                               // for HclConfig
#include "platform/gen2_arch_common/hcl_device_config.h"              // for HclDeviceConfig
#include "hcl_dynamic_comms_manager.h"                                // for HclDynamicCommsM...
#include "hcl_dynamic_communicator.h"                                 // for HclDynamicCommun...
#include "hcl_types.h"                                                // for RankInfo
#include "hcl_utils.h"                                                // for setLogContext
#include "hlthunk.h"                                                  // for hlthunk_requeste...
#include "platform/gen2_arch_common/eq_handler.h"                     // for IEventQueueHandler
#include "hcl_log_manager.h"                                          // for LOG_*
#include "platform/gen2_arch_common/intermediate_buffer_container.h"  // for IntermediateBufferContainer
#include "platform/gen2_arch_common/scaleout_provider.h"              // for ScaleoutProvider
#include "platform/gen2_arch_common/commands/hcl_commands.h"
#include "infra/scal/gen2_arch_common/scal_manager.h"  // for Gen2ArchScalManager
#include "platform/gen2_arch_common/commands/hcl_commands.h"
#include "ibverbs/hcl_ibverbs.h"
#include "hcl_math_utils.h"
#include "platform/gen2_arch_common/hcl_device_controller.h"
#include "platform/gen2_arch_common/server_def.h"                 // for Gen2ArchServerDef
#include "platform/gen2_arch_common/server_connectivity_types.h"  // for SCALEOUT_DEVICE_ID
#include "coordinator_defs.h"

class HclCommandsGen2Arch;
class DeviceBufferManager;

/* This is a test-only constructor, so the nic array in a few lines is allowed... :-\ */
HclDeviceGen2Arch::HclDeviceGen2Arch(const bool                   testCtor,
                                     HclDeviceControllerGen2Arch& controller,
                                     HclDeviceConfig&             deviceConfig,
                                     Gen2ArchServerDef&           serverDef)
: IHclDevice(deviceConfig),
  m_deviceController(controller),
  m_scalManager(controller.getGen2ArchScalManager()),
  m_commands(controller.getGen2ArchCommands()),
  m_cgSize(0),
  m_serverDef(serverDef),
  m_serverConnectivity(serverDef.getServerConnectivity())
{
    setLogContext(0, "localhost", (uint64_t)pthread_self());
    LOG_HCL_TRACE(HCL, "Test ctor, deviceType={}", deviceConfig.getDeviceTypeStr());
}

// Runtime ctor
HclDeviceGen2Arch::HclDeviceGen2Arch(HclDeviceControllerGen2Arch& controller,
                                     HclDeviceConfig&             deviceConfig,
                                     Gen2ArchServerDef&           serverDef)
: IHclDevice(deviceConfig),
  m_deviceController(controller),
  m_scalManager(controller.getGen2ArchScalManager()),
  m_commands(controller.getGen2ArchCommands()),
  m_cgSize(m_scalManager.getCgInfo(0)[(int)hcl::SchedulerType::external].size),
  m_serverDef(serverDef),
  m_serverConnectivity(serverDef.getServerConnectivity())
{
    LOG_HCL_TRACE(HCL, "Runtime ctor, deviceType={}", deviceConfig.getDeviceTypeStr());
    setLogContext(deviceConfig.getHwModuleId(), deviceConfig.getHostName(), (uint64_t)pthread_self());

    g_ibv.set_hcl_device(this);

    VERIFY(
        GCFG_HCL_GNIC_SCALE_OUT_QP_SETS.value() <= MAX_QPS_SETS_PER_CONNECTION,
        "HCL_GNIC_SCALE_OUT_QP_SETS (0x{:x}) is expected to be equal or less than MAX_QPS_SETS_PER_CONNECTION (0x{:x})",
        GCFG_HCL_GNIC_SCALE_OUT_QP_SETS.value(),
        MAX_QPS_SETS_PER_CONNECTION);
    VERIFY(GCFG_HCL_HNIC_SCALE_OUT_QP_SETS.value() <= MAX_HNIC_CONNECTION_SETS,
           "HCL_HNIC_SCALE_OUT_QP_SETS (0x{:x}) is expected to be equal or less than MAX_HNIC_CONNECTION_SETS (0x{:x})",
           GCFG_HCL_HNIC_SCALE_OUT_QP_SETS.value(),
           MAX_HNIC_CONNECTION_SETS);

    m_ethStats.init(m_deviceConfig.getDevicePciBusId());
}

uint32_t HclDeviceGen2Arch::createQpnInLKD(const uint32_t port, const uint8_t qpId)
{
    return g_ibv.create_qp(isSender(qpId), port);
}

bool HclDeviceGen2Arch::isNicUp(uint32_t nic)
{
    return g_ibv.is_nic_up(nic);
}

void HclDeviceGen2Arch::updateRankHasQp(const HCL_Comm comm, const HCL_Rank remoteRank)
{
    m_QpConnectionExistsForRank[comm].insert(remoteRank);
}

const std::set<HCL_Rank>& HclDeviceGen2Arch::getOpenScaleOutRanks(const HCL_Comm comm)
{
    static const std::set<HCL_Rank> emptySet;
    if (m_QpConnectionExistsForRank.size() > 0)
    {
        if (m_QpConnectionExistsForRank.count(comm) > 0)
        {
            return m_QpConnectionExistsForRank.at(comm);
        }
    }
    return emptySet;
};

hcl::Gen2ArchScalManager& HclDeviceGen2Arch::getScalManager()
{
    return m_scalManager;
}

DeviceBufferManager& HclDeviceGen2Arch::getSIB(const uint32_t streamIndex)
{
    return m_sibContainer->getSIB(streamIndex);
}

uint64_t HclDeviceGen2Arch::getSIBBufferSize() const
{
    return m_sibContainer->getBufferSize();
}

HclDeviceGen2Arch::~HclDeviceGen2Arch() noexcept(false)
{
    delete m_eqHandler;
    delete m_scaleoutProvider;
}

hcclResult_t HclDeviceGen2Arch::openQpsHLS(HCL_Comm comm)
{
    HclConfigType configType = (HclConfigType)GCFG_BOX_TYPE_ID.value();
    if (configType != getConfigType())
    {
        LOG_HCL_ERR(HCL, "Invalid config type ({}), expecting ({})", configType, getConfigType());
        return hcclInvalidArgument;
    }

    LOG_HCL_HEADER(HCL);

    LOG_HCL_INFO(HCL, "Open scale-up QPs");
    openQpsHlsScaleUp(comm);

    LOG_HCL_INFO(HCL, "Open scale-out connections, QP Spray factor: {}", getComm(comm).getMaxScaleOutQpSetsNum());
    UniqueSortedVector outerRanks;
    getOuterRanks(comm, outerRanks);
    m_scaleoutProvider->openConnectionsOuterRanks(comm, outerRanks);

    return hcclSuccess;
}

nics_mask_t HclDeviceGen2Arch::getActiveNics(HCL_Rank fromRank, HCL_Rank toRank, int physicalQueueOffset, HCL_Comm comm)
{
    // should not happen
    VERIFY(fromRank != toRank, "getActiveNics called with same rank({})", fromRank);

    std::pair<HCL_Rank, HCL_Rank> ranksPair {fromRank, toRank};

    auto it = m_activeNicsSingleRankCache[comm].find(ranksPair);
    if (it != m_activeNicsSingleRankCache[comm].end())
    {
        return it->second;
    }

    const int deviceId = getComm(comm).isRankInsideScaleupGroup(toRank)
                             ? getComm(comm).m_remoteDevices[toRank]->header.hwModuleID
                             : SCALEOUT_DEVICE_ID;

    const nics_mask_t result = getAllPorts(deviceId, getComm(comm).getCommConnectivity().getExternalPortsMask());

    VERIFY(result.count() <= ((SCALEOUT_DEVICE_ID == (unsigned)deviceId)
                                  ? getServerConnectivity().getMaxNumScaleOutPorts(/* ? HCL_Comm comm*/)
                                  : getServerConnectivity().getMaxNumScaleUpPortsPerConnection(comm)),

           "invalid number of active nics({}) from rank({}) to rank({})",

           result.count(),
           fromRank,
           toRank);

    m_activeNicsSingleRankCache[comm][ranksPair] = result;

    // init the RemoteInfo indexToNic mapping
    if (fromRank == getMyRank(comm))
    {
        // direct access to qp data, to set nic
        GaudiNicQPs::NicQPs* qps   = getComm(comm).m_rankInfo.remoteInfo[toRank].gaudiNicQPs.qp;
        uint32_t             index = 0;
        for (uint8_t nic : m_activeNicsSingleRankCache[comm][ranksPair])
        {
            qps[index++].nic = nic;
        }
    }

    return m_activeNicsSingleRankCache[comm][ranksPair];
}

nics_mask_t HclDeviceGen2Arch::getAllPorts(const int deviceId, const nics_mask_t enabledExternalPortsMask) const
{
    return getServerConnectivity().getAllPorts(deviceId, enabledExternalPortsMask);
};

bool HclDeviceGen2Arch::isScaleOutPort(const uint16_t port, const HCL_Comm comm) const
{
    return getServerConnectivity().isScaleoutPort(port, comm);
}

hcclResult_t HclDeviceGen2Arch::onNewCommStart(HCL_Comm comm, uint32_t commSize, HclConfig& config)
{
    VERIFY(config.m_jsonIndex != -1);

    // get comm
    getComm(comm).m_rankInfo.device.m_comm = comm;

    // get my rank
    getComm(comm).m_rankInfo.header.hcclRank = config.m_jsonIndex;

    // get and save mac address from all available ports
    fillMacAddresses(comm);

    // get and save device info
    getDeviceConfig().fillDeviceInfo(getComm(comm).m_rankInfo.header);

    return hcclSuccess;
}

hcclResult_t HclDeviceGen2Arch::destroyComm(HCL_Comm comm, bool force)
{
    LOG_HCL_INFO(HCL, "starting to destroy communicator ({})...", comm);
    deleteCommConnections(comm);
    m_dynamicComms.destroyComm(comm);
    return hcclSuccess;
}

void HclDeviceGen2Arch::deleteCommConnections(HCL_Comm comm)
{
    QPManagerHints hints(comm);
    for (unsigned nic = 0; nic < MAX_NICS_GEN2ARCH; nic++)
    {
        hints.m_nic = nic;
        m_qpManagers.at(nic)->ReleaseQPsResource(hints);
    }

    LOG_INFO(HCL, "Close scale-out connections");
    m_scaleoutProvider->closeConnections(comm);
}

void HclDeviceGen2Arch::checkSignals()
{
    LOG_HCL_DEBUG(HCL, "Started");

    bool failedCheckSignals = false;
    bool anyRegNonZero      = false;
    for (size_t archIndex = 0; archIndex < hcl::ScalJsonNames::numberOfArchsStreams; archIndex++)
    {
        int rc = 0;

        const uint64_t longSoIndex = m_scalManager.getCgInfo(archIndex)[(int)hcl::SchedulerType::internal].longSoIndex;
        LOG_HCL_DEBUG(HCL, "archIndex={}, longSoIndex=0x{:x}", archIndex, longSoIndex);

        const uint64_t longSoAddr = m_scalManager.getCgInfo(archIndex)[(int)hcl::SchedulerType::internal].longSoAddr;
        LOG_HCL_DEBUG(HCL, "archIndex={}, longSoAddr=0x{:x}", archIndex, longSoAddr);

        // read long SO which consists of 4 32 bits regs, 15 bits in each
        constexpr size_t                   LONG_SO_SIZE = 4;
        std::array<uint32_t, LONG_SO_SIZE> longSoBuff;

        rc = hlthunk_device_memory_read_block_experimental(getFd(),
                                                           &(longSoBuff[0]),
                                                           longSoAddr,
                                                           LONG_SO_SIZE * sizeof(uint32_t),
                                                           0);
        if (rc != 0)
        {
            LOG_HCL_WARN(HCL,
                         "Failed to read long so, archIndex={}, longSoAddr=0x{:x}, status: [{}]",
                         archIndex,
                         longSoAddr,
                         rc);
        }
        else
        {
            // construct long SO value from the 4 regs. The LSB is the first reg
            uint64_t longSoValue = 0;
            for (size_t longSoRegIndex = LONG_SO_SIZE; longSoRegIndex > 0; longSoRegIndex--)
            {
                const uint32_t longSoRegValue = longSoBuff[longSoRegIndex - 1];
                longSoValue                   = (longSoValue << 15) + longSoRegValue;
                LOG_HCL_DEBUG(HCL,
                              "archIndex={}, longSoRegIndex={}, longSoRegValue=0x{:x}, longSoValue=0x{:x}",
                              archIndex,
                              longSoRegIndex - 1,
                              longSoRegValue,
                              longSoValue);
            }

            const uint64_t cgAddr = m_scalManager.getCgInfo(archIndex)[(int)hcl::SchedulerType::internal].cgBaseAddr;
            const unsigned cgSize = m_scalManager.getCgInfo(archIndex)[(int)hcl::SchedulerType::internal].size;
            LOG_HCL_DEBUG(HCL, "archIndex={}, cgAddr=0x{:x}, cgSize={}", archIndex, cgAddr, cgSize);

            std::unique_ptr<uint32_t[]> buff(new uint32_t[cgSize] {});
            rc = hlthunk_device_memory_read_block_experimental(getFd(), buff.get(), cgAddr, cgSize, 0);
            if (rc != 0)
            {
                LOG_HCL_WARN(HCL,
                             "Failed to read registers, archIndex={}, cgAddr=0x{:x}, cgSize={}, status: [{}]",
                             archIndex,
                             cgAddr,
                             cgSize,
                             rc);
            }
            else
            {
                const size_t soOffset = mod(longSoValue, cgSize);
                for (size_t regIndex = 0; regIndex < cgSize; regIndex++)
                {
                    LOG_HCL_DEBUG(HCL,
                                  "archIndex={}, addr=0x{:x}, content=0x{:x}",
                                  archIndex,
                                  cgAddr + regIndex,
                                  buff[regIndex]);
                    // At end of run, its possible for exactly one CG to have a value of 1,
                    // due to the force order mechanism (last op advanced the next SO by 1)
                    // This CG index is calculated from the longSoValue modulus cgSize.
                    if ((buff[regIndex] != 0) && !((regIndex == soOffset) && (buff[regIndex] == 1)))
                    {
                        anyRegNonZero = true;
                        LOG_HCL_ERR(HCL,
                                    "Non zero register, archIndex={}, addr=0x{:x}, content=0x{:x}",
                                    archIndex,
                                    cgAddr + regIndex,
                                    buff[regIndex]);
                    }
                }
            }
        }

        if (anyRegNonZero)
        {
            failedCheckSignals = true;
            LOG_HCL_ERR(HCL, "archIndex={}, Signals check failed", archIndex);
        }
        LOG_HCL_DEBUG(HCL, "archIndex={}, failedCheckSignals={}", archIndex, failedCheckSignals);
    }

    if (GCFG_HCL_FAIL_ON_CHECK_SIGNALS.value())
    {
        VERIFY(!failedCheckSignals, "Gen2 failed signals check, check log files");
    }
}

hcclResult_t HclDeviceGen2Arch::destroy(bool force)
{
    if (isCommExist(HCL_COMM_WORLD))
    {
        destroyComm(HCL_COMM_WORLD, force);
    }
    m_eqHandler->stopThread();

    checkSignals();

    m_sibContainer->destroy();

    getScaleOutProvider()->destroy();
    return hcclSuccess;
}

hcclResult_t HclDeviceGen2Arch::establishQpConnectionWithPeerQp(const HCL_Comm comm,
                                                                const HCL_Rank rank,
                                                                const uint32_t stream,
                                                                const uint32_t port,
                                                                const uint32_t qpn,
                                                                const uint8_t  qpSet)
{
    const uint16_t   peerNic          = getPeerNic(rank, comm, port);
    GaudiNicAddress& remoteNicAddress = getComm(comm).m_remoteDevices[rank]->device.gaudiNicAddresses.nics[peerNic];

    GaudiNicQPs::NicQPs& remoteQPs = getComm(comm).m_remoteDevices[rank]->remoteInfo.gaudiNicQPs[peerNic];
    uint32_t             qpi       = getQpi(comm, port, rank, qpn, qpSet);
    GaudiNicAddress&     srcNic    = getComm(comm).m_rankInfo.device.gaudiNicAddresses.nics[port];

    uint8_t lagIdx, lastInLag;
    getLagInfo(port, lagIdx, lastInLag, comm);

    LOG_HCL_TRACE(HCL,
                  "comm({}), rank({}), stream({}), port({}), peerNic={}, qpn({}), qpSet({}) calling getDestQpi({})",
                  comm,
                  rank,
                  stream,
                  port,
                  peerNic,
                  qpn,
                  qpSet,
                  qpi);

    g_ibv.set_qp_ctx(qpn,
                     port,
                     srcNic.ip,
                     srcNic.mac.u64,
                     remoteNicAddress.ip,
                     remoteNicAddress.mac.u64,
                     remoteQPs.qp[qpSet][getDestQpi(qpi, port)],
                     lagIdx,
                     lastInLag);

    return hcclSuccess;
}

bool HclDeviceGen2Arch::isDramAddressValid(uint64_t addr) const
{
    return (addr >= m_allocationRangeStart && addr < m_allocationRangeEnd);
}

void HclDeviceGen2Arch::getLagInfo(const uint16_t nic, uint8_t& lagIdx, uint8_t& lastInLag, const HCL_Comm comm)
{
    lagIdx    = 0;
    lastInLag = false;
}

HclCommandsGen2Arch& HclDeviceGen2Arch::getGen2ArchCommands()
{
    return m_commands;
}

ScaleoutProvider* HclDeviceGen2Arch::getScaleOutProvider()
{
    return m_scaleoutProvider;
}

extern std::unordered_map<HCL_Comm, spHcclCoordinatorClient> g_hcclCordClient;

void HclDeviceGen2Arch::openAllRequiredNonPeerQPs(const HCL_Comm comm, const std::set<HCL_Rank>& remoteRanks)
{
    LOG_HCL_TRACE(HCL, "comm={}, remoteRanks.size={}", comm, remoteRanks.size());
    if (((HclConfigType)GCFG_BOX_TYPE_ID.value() == LOOPBACK) || GCFG_HCL_NULL_SUBMIT.value()) return;

    const bool         isHnicsScaleout = m_scaleoutProvider->isHostNic();
    UniqueSortedVector nonPeerRemoteRanks;
    for (const HCL_Rank remoteRank : remoteRanks)
    {
        LOG_HCL_TRACE(HCL, "Checking if opened, comm={}, remoteRank={}", comm, remoteRank);

        if (m_QpConnectionExistsForRank[comm].count(remoteRank) == 0)
        {
            LOG_HCL_TRACE(HCL, "Need to handle, comm({})  remoteRank({})", comm, remoteRank);
            nonPeerRemoteRanks.insert_sorted(remoteRank);
        }
    }

    if (nonPeerRemoteRanks.size() == 0)
    {
        return;
    }

    LOG_HCL_TRACE(HCL, "nonPeerRemoteRanks={}", nonPeerRemoteRanks);
    m_scaleoutProvider->openConnectionsOuterRanks(comm, nonPeerRemoteRanks);

    LOG_HCL_INFO(HCL, "Open scale-out connections to remote non-peer ranks");

    const size_t sendRecvBufSize = isHnicsScaleout ? sizeof(HostNicConnectInfo) : sizeof(RemoteDeviceConnectionInfo);
    std::vector<HostNicConnectInfo> hnicsConnectionInfoBuffers(nonPeerRemoteRanks.size());  // used by host nics
    size_t                          ranksCounter = 0;

    std::vector<void*> sendBuffers, recvBuffers;

    LOG_HCL_TRACE(HCL, "Exchanging connections info from remote ranks - async recv");

    VERIFY(g_hcclCordClient[comm].get());

    for (const HCL_Rank remoteRank : nonPeerRemoteRanks)
    {
        void* recvBuffer = nullptr;

        if (isHnicsScaleout)
        {
            auto& bufferFromTarget = hnicsConnectionInfoBuffers[ranksCounter++];
            recvBuffer             = &bufferFromTarget;
        }
        else
        {
            recvBuffer = getComm(comm).m_remoteDevices[remoteRank].get();
        }

        recvBuffers.push_back(recvBuffer);
    }

    std::vector<RemoteDeviceConnectionInfo> rdInfo;
    rdInfo.resize(nonPeerRemoteRanks.size());
    ranksCounter = 0;

    for (const HCL_Rank remoteRank : nonPeerRemoteRanks)
    {
        void* sendBuffer = nullptr;

        if (isHnicsScaleout)
        {
            auto& bufferToTarget = getComm(comm).m_rankInfo.remoteInfo[remoteRank].hostNicConns;
            sendBuffer           = &bufferToTarget;
        }
        else
        {
            RemoteDeviceConnectionInfo connectionInfo;
            // extract remote device connection info for remoteRank
            connectionInfo.header     = getComm(comm).m_rankInfo.header;
            connectionInfo.device     = getComm(comm).m_rankInfo.device;
            connectionInfo.remoteInfo = getComm(comm).m_rankInfo.remoteInfo[remoteRank];

            rdInfo[ranksCounter] = connectionInfo;
            sendBuffer           = &rdInfo[ranksCounter];
            ranksCounter++;
        }

        sendBuffers.push_back(sendBuffer);
    }

    g_hcclCordClient[comm]->sendRecvFromRanks(nonPeerRemoteRanks, recvBuffers, sendBuffers, sendRecvBufSize);

    LOG_HCL_TRACE(HCL, "Updating connections info with remote ranks");
    m_scaleoutProvider->updateConnectionsNonPeer(comm, nonPeerRemoteRanks, hnicsConnectionInfoBuffers);
    VERIFY(g_hcclCordClient[comm]->rendezvous(nonPeerRemoteRanks), "Failed to synchronize remote ranks");
}

unsigned HclDeviceGen2Arch::getEdmaEngineWorkDistributionSize()
{
    return edmaEngineGroupSizes[0];
}

void HclDeviceGen2Arch::destroyQp(uint32_t port, uint32_t qpn)
{
    g_ibv.destroy_qp(port, qpn);
}

void HclDeviceGen2Arch::dfa(hl_logger::LoggerSPtr logger)
{
    m_ethStats.dump(logger, false);
}

uint64_t HclDeviceGen2Arch::getDRAMSize()
{
    uint64_t scalBase, hbmPoolStart, allocatedSize;
    getScalManager().getHBMInfoForExport(scalBase, hbmPoolStart, allocatedSize);
    return allocatedSize;
}

uint64_t HclDeviceGen2Arch::getDRAMBaseAddr()
{
    uint64_t scalBase, hbmPoolStart, allocatedSize;
    getScalManager().getHBMInfoForExport(scalBase, hbmPoolStart, allocatedSize);
    return hbmPoolStart;
}

void HclDeviceGen2Arch::setGaudiDirect()
{
    if (GCFG_HCCL_GAUDI_DIRECT.isSetFromUserConfig())
    {
        return;
    }
    // Using hl_gcfg::setGcfgItemValue instead of setValue() to enable reading the value later with
    // synConfigurationGet()
    hl_gcfg::setGcfgItemValue("HCCL_GAUDI_DIRECT", "true");
    LOG_HCL_INFO(HCL, "Attempting to use gaudi direct");
}

uint8_t HclDeviceGen2Arch::getNumQpSets(bool isScaleOut, HCL_Comm comm, HCL_Rank remoteRank)
{
    return isScaleOut && getComm(comm).isPeer(remoteRank) ? getComm(comm).getMaxScaleOutQpSetsNum() : 1;
}

bool HclDeviceGen2Arch::isACcbHalfFullForDeviceBenchMark(const unsigned archStreamIdx)
{
    return getScalManager().isACcbHalfFullForDeviceBenchMark(archStreamIdx);
};

/**
 * @brief map remote info index to nic for loopback mode
 *        workaround for loopback mode, where getActiveNics is not used
 *        and RemoteInfo.indexToNic is not initialized, just so it will be initialized
 */
void HclDeviceGen2Arch::initRemoteNicsLoopback(const HCL_Comm comm)
{
    LOG_HCL_DEBUG(HCL, "Init loopback remote nics comm({})", getCommSize(comm));

    nics_mask_t scaleupNics  = getServerConnectivity().getScaleUpPorts(comm);
    nics_mask_t scaleoutNics = getServerConnectivity().getScaleOutPortsGlbl(comm);

    int scaleupNicIndex = 0, nic = 0;
    for (HCL_Rank rank = 0; rank < getCommSize(comm); rank++)
    {
        if (rank == getMyRank(comm)) continue;

        int scaleoutNicIndex = 0;

        // direct access to qp data, to set nic
        GaudiNicQPs::NicQPs* qps = getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs.qp;
        for (size_t qpIndex = 0; qpIndex < COMPACT_RANK_INFO_NICS; qpIndex++)
        {
            if (rank < getScaleupGroupSize(comm))
            {
                nic = scaleupNics(scaleupNicIndex);
                scaleupNicIndex++;
            }
            else
            {
                nic = scaleoutNics(scaleoutNicIndex);
                scaleoutNicIndex++;
            }
            qps[qpIndex].nic = nic;
        }
        LOG_HCL_DEBUG(HCL, "Rank({}) mapped to ({}, {}, {})", rank, qps[0].nic, qps[1].nic, qps[2].nic);
    }
}

uint16_t HclDeviceGen2Arch::getMaxNumScaleUpPortsPerConnection(const HCL_Comm hclCommId) const
{
    return getServerConnectivity().getMaxNumScaleUpPortsPerConnection(hclCommId);
}

uint32_t HclDeviceGen2Arch::getDestQpi(const unsigned qpi, const unsigned nic) const
{
    return m_qpManagers.at(nic)->getDestQPi(qpi);
}

void HclDeviceGen2Arch::allocateQPDBStorage(const HCL_Comm comm)
{
    // this is used for null-submit mode only, we allocate QP storage without the actual QPs
    for (unsigned nic = 0; nic < MAX_NICS_GEN2ARCH; nic++)
    {
        m_qpManagers.at(nic)->allocateQPDBStorage(comm);
    }
}

void HclDeviceGen2Arch::setTraceMarker(const synStreamHandle stream_handle, uint32_t val)
{
    int archStreamIdx = synStreamGetPhysicalQueueOffset(stream_handle);
    m_deviceController.setTraceMarker(archStreamIdx, val);
}

void HclDeviceGen2Arch::getAsyncError(const std::vector<HCL_HwModuleId> remoteModuleIDs,
                                      const HCL_Comm                    comm,
                                      hcclResult_t*                     asyncError)
{
    // for each remote module ID, get all the nics that connect it to this rank, and check if any nic is down
    for (auto& remoteModuleID : remoteModuleIDs)
    {
        const nics_mask_t nicsToRemote = m_serverConnectivity.getAllPortsGlbl(remoteModuleID, comm);
        for (auto nic : nicsToRemote)
        {
            if (m_hclNic.mask[nic] && !m_hclNic.state[nic])
            {
                LOG_HCL_ERR(HCL, "NIC {} is down", nic);
                *asyncError = hcclPortDown;
                return;
            }
        }
    }

    *asyncError = hcclSuccess;
}
