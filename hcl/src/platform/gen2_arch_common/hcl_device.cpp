#include "platform/gen2_arch_common/hcl_device.h"

#include <pthread.h>                                                    // for pthread_self
#include <cstring>                                                      // for memset
#include <memory>                                                       // for __shared_ptr_access
#include "hcl_config.h"                                                 // for HclDeviceConfig
#include "hcl_dynamic_comms_manager.h"                                  // for HclDynamicCommsM...
#include "hcl_dynamic_communicator.h"                                   // for HclDynamicCommun...
#include "hcl_types.h"                                                  // for RankInfo
#include "hcl_utils.h"                                                  // for setLogContext
#include "hlthunk.h"                                                    // for hlthunk_requeste...
#include "platform/gen2_arch_common/eq_handler.h"                       // for IEventQueueHandler
#include "platform/gen2_arch_common/port_mapping.h"                     // for Gen2ArchDevicePortMapping
#include "hcl_log_manager.h"                                            // for LOG_*
#include "platform/gen2_arch_common/intermediate_buffer_container.h"    // for IntermediateBufferContainer
#include "platform/gen2_arch_common/scaleout_provider.h"                // for ScaleoutProvider
#include "platform/gen2_arch_common/commands/hcl_commands.h"
#include "hccl_coordinator_client.h"
#include "infra/scal/gen2_arch_common/scal_manager.h"  // for Gen2ArchScalManager
#include "platform/gen2_arch_common/commands/hcl_commands.h"
#include "ibverbs/hcl_ibverbs.h"
#include "hcl_math_utils.h"
#include "libfabric/mr_mapping.h"  // for MRMapping
#include "platform/gen2_arch_common/hcl_device_controller.h"
#include "platform/gen2_arch_common/port_mapping_config.h"  // for SCALEOUT_DEVICE_ID

class HclCommandsGen2Arch;
class DeviceBufferManager;

/* This is a test-only constructor, so the nic array in a few lines is allowed... :-\ */
HclDeviceGen2Arch::HclDeviceGen2Arch(HclDeviceControllerGen2Arch& controller)
: IHclDevice(),
  m_scalManager(controller.getGen2ArchScalManager()),
  m_commands(controller.getGen2ArchCommands()),
  m_cgSize(0)
{
    setLogContext(0, "localhost", (uint64_t)pthread_self());
}

HclDeviceGen2Arch::HclDeviceGen2Arch(HclDeviceControllerGen2Arch& controller, HclDeviceConfig& deviceConfig)
: IHclDevice(deviceConfig),
  m_scalManager(controller.getGen2ArchScalManager()),
  m_deviceType(deviceConfig.m_deviceType),
  m_commands(controller.getGen2ArchCommands()),
  m_cgSize(m_scalManager.getCgInfo(0)[(int)hcl::SchedulerType::external].size)
{
    setLogContext(deviceConfig.getHwModuleId(), deviceConfig.getHostName(), (uint64_t)pthread_self());

    VERIFY(GCFG_HCL_GNIC_SCALE_OUT_QP_SETS.value() <= MAX_QPS_SETS_PER_CONNECTION,
        "HCL_GNIC_SCALE_OUT_QP_SETS (0x{:x}) is expected to be equal or less than MAX_QPS_SETS_PER_CONNECTION (0x{:x})",
        GCFG_HCL_GNIC_SCALE_OUT_QP_SETS.value(),
           MAX_QPS_SETS_PER_CONNECTION);
    VERIFY(GCFG_HCL_HNIC_SCALE_OUT_QP_SETS.value() <= MAX_HNIC_CONNECTION_SETS,
           "HCL_HNIC_SCALE_OUT_QP_SETS (0x{:x}) is expected to be equal or less than MAX_HNIC_CONNECTION_SETS (0x{:x})",
           GCFG_HCL_HNIC_SCALE_OUT_QP_SETS.value(),
           MAX_HNIC_CONNECTION_SETS);

    char busId[13] {};
    int  res = hlthunk_get_pci_bus_id_from_fd(deviceConfig.m_fd, busId, sizeof(busId));
    if (res != 0)
    {
        LOG_ERR(HCL, "Failed to get busId from fd {} for interfaces", deviceConfig.m_fd);
    }

    m_ethStats.init(busId);
    m_portMappingConfig.parseConfig(GCFG_HCL_PORT_MAPPING_CONFIG.value());  // parse json port mapping file if exists
}

uint32_t HclDeviceGen2Arch::createQp(uint32_t port, uint8_t qpId)
{
    return g_ibv.create_qp(isSender(qpId), port) ;
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
    delete m_sibContainer;
    delete m_scaleoutProvider;

    g_ibv.close();
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

nics_mask_t
HclDeviceGen2Arch::getActiveNics(HCL_Rank fromRank, HCL_Rank toRank, int physicalQueueOffset, HCL_Comm comm)
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

    nics_mask_t result = getAllPorts(deviceId, getComm(comm).getSpotlightType());

    VERIFY(result.count() <= ((SCALEOUT_DEVICE_ID == (unsigned)deviceId)
                                  ? getPortMapping().getMaxNumScaleOutPorts()
                                  : getHal()->getMaxNumScaleUpPortsPerConnection()),

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

void HclDeviceGen2Arch::checkSignals()
{
    LOG_HCL_DEBUG(HCL, "Started");

    bool failedCheckSignals = false;
    bool anyRegNonZero = false;
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

    getScaleOutProvider()->destroy();
    return hcclSuccess;
}

hcclResult_t
HclDeviceGen2Arch::setupQps(HCL_Comm comm, HCL_Rank rank, uint32_t stream, uint32_t port, uint32_t qpn, uint8_t qpSet)
{
    const uint16_t   peerNic = getPeerNic(rank, comm, port);
    GaudiNicAddress& remoteNicAddress =
        getComm(comm).m_remoteDevices[rank]->device.gaudiNicAddresses.nics[peerNic];

    GaudiNicQPs::NicQPs& remoteQPs =
        getComm(comm).m_remoteDevices[rank]->remoteInfo.gaudiNicQPs[peerNic];
    uint32_t         qpi    = getQpi(comm, port, rank, qpn, qpSet);
    GaudiNicAddress& srcNic = getComm(comm).m_rankInfo.device.gaudiNicAddresses.nics[port];

    uint8_t lagIdx, lastInLag;
    getLagInfo(port, lagIdx, lastInLag, getComm(comm).getSpotlightType());

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
                    remoteQPs.qp[qpSet][getDestQpi(qpi)],
                     lagIdx,
                     lastInLag);

    return hcclSuccess;

}

bool HclDeviceGen2Arch::isDramAddressValid(uint64_t addr) const
{
    return (addr >= m_allocationRangeStart && addr < m_allocationRangeEnd);
}

void HclDeviceGen2Arch::getLagInfo(int nic, uint8_t& lagIdx, uint8_t& lastInLag, unsigned spotlightType)
{
    lagIdx    = 0;
    lastInLag = false;
}

HclCommandsGen2Arch& HclDeviceGen2Arch::getGen2ArchCommands()
{
    return m_commands;
}

uint64_t HclDeviceGen2Arch::getEnabledPortsMask()
{
    VERIFY(false, "HclDeviceGen2Arch::getEnabledPortsMask() not supported!");
    return 0;
}

ScaleoutProvider* HclDeviceGen2Arch::getScaleOutProvider()
{
    return m_scaleoutProvider;
}

hcclResult_t HclDeviceGen2Arch::openQps(HCL_Comm comm, const UniqueSortedVector& ranks)
{
    VERIFY(false, "HclDeviceGen2Arch::openQps - not implemented yet");

    return hcclSuccess;
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

    LOG_HCL_TRACE(HCL, "Exchanging connections info from remote ranks - async recv");
    std::vector<std::unique_ptr<hcclHandle>> recvHandles;
    for (const HCL_Rank remoteRank : nonPeerRemoteRanks)
    {
        VERIFY(g_hcclCordClient[comm].get());
        recvHandles.emplace_back(std::make_unique<hcclHandle>());

        void*        recvBuffer = nullptr;
        const size_t recvSize   = sendRecvBufSize;
        if (isHnicsScaleout)
        {
            auto& bufferFromTarget = hnicsConnectionInfoBuffers[ranksCounter];
            recvBuffer             = &bufferFromTarget;
        }
        else
        {
            recvBuffer = getComm(comm).m_remoteDevices[remoteRank].get();
        }
        LOG_HCL_TRACE(HCL,
                      "Calling recvFromRankAsync, comm({}), remoteRank({}), recvBuffer={:p}, recvSize={}",
                      comm,
                      remoteRank,
                      recvBuffer,
                      recvSize);
        const hcclResult_t ret =
            g_hcclCordClient[comm]->recvFromRankAsync(recvBuffer, recvSize, remoteRank, &(*(recvHandles.back())));
        VERIFY(ret == hcclSuccess, "recvFromRankAsync RankInfo failed, ret={}, remoteRank={}", ret, remoteRank);
        ranksCounter++;
    }

    LOG_HCL_TRACE(HCL, "Exchanging connections info from remote ranks - sync send");
    for (const HCL_Rank remoteRank : nonPeerRemoteRanks)
    {
        void*                      sendBuffer = nullptr;
        const size_t               sendSize   = sendRecvBufSize;
        RemoteDeviceConnectionInfo connectionInfo;
        if (isHnicsScaleout)
        {
            auto& bufferToTarget = getComm(comm).m_rankInfo.remoteInfo[remoteRank].hostNicConns;
            sendBuffer           = &bufferToTarget;
        }
        else
        {
            // extract remote device connection info for remoteRank
            connectionInfo.header     = getComm(comm).m_rankInfo.header;
            connectionInfo.device     = getComm(comm).m_rankInfo.device;
            connectionInfo.remoteInfo = getComm(comm).m_rankInfo.remoteInfo[remoteRank];
            sendBuffer                = &connectionInfo;
        }

        LOG_HCL_TRACE(HCL,
                      "Calling sendToRank, comm({}), remoteRank({}), sendBuffer={:p}, sendSize={}",
                      comm,
                      remoteRank,
                      sendBuffer,
                      sendSize);
        const hcclResult_t ret = g_hcclCordClient[comm]->sendToRank(remoteRank, sendBuffer, sendSize);
        VERIFY(ret == hcclSuccess, "sendToRank RankInfo failed, ret{}, remoteRank={}", ret, remoteRank);
    }

    LOG_HCL_TRACE(HCL, "Exchanging connections info from remote ranks - wait for recv");
    for (const HCL_Rank remoteRank : nonPeerRemoteRanks)
    {
        LOG_HCL_TRACE(HCL, "Calling waitForHandle & updateRankQps, comm={}, remoteRank={}", comm, remoteRank);

        VERIFY(recvHandles.front()->internalHandle.waitForHandle(),
               "waitForHandle RankInfo failed, remoteRank={}",
               remoteRank);
        recvHandles.erase(recvHandles.begin());  // call dtor
    }
    VERIFY(recvHandles.size() == 0, "recvHandles is not empty, {}", recvHandles.size());

    LOG_HCL_TRACE(HCL, "Updating connections info with remote ranks");
    m_scaleoutProvider->updateConnectionsNonPeer(comm, nonPeerRemoteRanks, hnicsConnectionInfoBuffers);
    synchronizeRemoteRanks(comm, nonPeerRemoteRanks);
}

void HclDeviceGen2Arch::synchronizeRemoteRanks(const HCL_Comm comm, const UniqueSortedVector& remoteRanks)
{
    // This section synchronize all the remote ranks using the coordinator
    LOG_HCL_TRACE(HCL, "Synchronize with all remote ranks - comm={}, , remoteRanks={}", comm, remoteRanks);

    std::vector<std::unique_ptr<hcclHandle>> recvHandles;
    std::vector<int>                               recvAckKeys(remoteRanks.size(), 0);
    unsigned                                       recvAckCount = 0;
    for (const HCL_Rank remoteRank : remoteRanks)
    {
        LOG_HCL_TRACE(HCL, "Calling recvFromRankAsync ack, comm={}, remoteRank={}", comm, remoteRank);

        recvHandles.emplace_back(std::make_unique<hcclHandle>());
        int*               ackPtr(&recvAckKeys[recvAckCount++]);
        const hcclResult_t ret =
            g_hcclCordClient[comm]->recvFromRankAsync(ackPtr, sizeof(*ackPtr), remoteRank, &(*(recvHandles.back())));
        VERIFY(ret == hcclSuccess, "recvFromRankAsync ack failed, ret={}, remoteRank={}", ret, remoteRank);
    }

    LOG_HCL_TRACE(HCL, "Synchronize with all remote ranks - sync send");
    static int ackKey = 0xABC;
    for (const HCL_Rank remoteRank : remoteRanks)
    {
        LOG_HCL_TRACE(HCL, "Calling sendToRank ack, comm={}, remoteRank={}", comm, remoteRank);

        const hcclResult_t ret = g_hcclCordClient[comm]->sendToRank(remoteRank, &ackKey, sizeof(ackKey));
        VERIFY(ret == hcclSuccess, "sendToRank ack failed, ret={}, remoteRank={}", ret, remoteRank);
    }

    LOG_HCL_TRACE(HCL, "Synchronize with all remote ranks - wait for recv");
    recvAckCount = 0;
    for (const HCL_Rank remoteRank : remoteRanks)
    {
        LOG_HCL_TRACE(HCL, "Calling waitForHandle ack, comm={}, remoteRank={}", comm, remoteRank);

        const int* ackPtr(&recvAckKeys[recvAckCount++]);
        VERIFY(recvHandles.front()->internalHandle.waitForHandle(),
               "waitForHandle ack failed, remoteRank={}",
               remoteRank);
        VERIFY(*ackPtr == ackKey,
               "ackKey verification failed, received key=0x{:x} from remoteRank={}, expected key=0x{}",
               *ackPtr,
               remoteRank,
               ackKey);
        recvHandles.erase(recvHandles.begin());  // call dtor
        LOG_HCL_TRACE(HCL, "waitForHandle ack completed successfully, comm={}, remoteRank={}", comm, remoteRank);
    }

    VERIFY(recvHandles.size() == 0, "After ack recvHandles is not empty, {}", recvHandles.size());
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

void HclDeviceGen2Arch::exportHBMMR()
{
    if (!getScaleOutProvider()->isHostNic())
    {
        return;
    }

    ofi_component_t* ofiComponent = getOfiComponent();
    VERIFY(ofiComponent != nullptr, "ofiComponent was not initialized");
    if (!ofi_t::isGaudiDirect())
    {
        return;
    }

    uint64_t scalBase, hbmPoolStart, allocatedSize;
    getScalManager().getHBMInfoForExport(scalBase, hbmPoolStart, allocatedSize);
    uint64_t offset = hbmPoolStart - scalBase;
    LOG_HCL_DEBUG(HCL,
                  "Mapping device memory: base addr 0x{:x} offset 0x{:x} size {:g}MB",
                  scalBase,
                  offset,
                  B2MB(allocatedSize));
    VERIFY(MRMapping::get_instance().mapDevMem(scalBase, allocatedSize, offset, (O_RDWR | O_CLOEXEC), ofiComponent) ==
               hcclSuccess,
           "device buffer mapping for OFI gaudi-direct failed.");
    if (ofi_t::isFabricFlush())
    {
        VERIFY(MRMapping::get_instance().mapFlushBufMem(ofiComponent) == hcclSuccess,
               "Flush buffer mapping for gaudi-direct failed");
    }
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
    GCFG_HCCL_GAUDI_DIRECT.setValue(true);
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
void HclDeviceGen2Arch::initRemoteNicsLoopback(HCL_Comm comm)
{
    LOG_HCL_DEBUG(HCL, "Init loopback remote nics comm({})", getCommSize(comm));
    for (int rank = 0; rank < getCommSize(comm); rank++)
    {
        // direct access to qp data, to set nic
        GaudiNicQPs::NicQPs* qps = getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs.qp;
        for (size_t index = 0; index < COMPACT_RANK_INFO_NICS; index++)
        {
            qps[index].nic = LOOPBACK_NIC_INDEX_INIT(index, rank);
        }
        LOG_HCL_DEBUG(HCL, "Rank({}) mapped to ({}, {}, {})", rank, qps[0].nic, qps[1].nic, qps[2].nic);
    }
}
