#include "interfaces/hcl_idevice.h"

#include <cstring>                                // for memset, memcpy, NULL
#include <array>                                  // for array
#include <cstdint>                                // for uint32_t, uint8_t
#include <memory>                                 // for __shared_ptr_access
#include <set>                                    // for set
#include <string>                                 // for string
#include <utility>                                // for pair

#include "hlthunk.h"                              // for hlthunk_device_name, hlthunk_...
#include "hcl_api_types.h"                        // for HCL_Comm, HCL_Rank
#include "hcl_config.h"                           // for HclDeviceConfig
#include "hcl_dynamic_comms_manager.h"            // for HclDynamicCommsManager
#include "hcl_dynamic_communicator.h"             // for HclDynamicCommunicator
#include "hcl_global_conf.h"                      // for GlobalConfImpl::value
#include "hcl_nic.h"                              // for HclNic
#include "interfaces/hcl_remote_device.h"         // for HclRemoteDevice
#include "hcl_utils.h"                            // for macAddr2Str, VERIFY
#include "interfaces/hcl_hal.h"                   // for HalPtr, Hal
#include "interfaces/hcl_unique_sorted_vector.h"  // for UniqueSortedVector
#include "libfabric/hl_ofi.h"                     // for ofi_t
#include "ofi_plugin.h"                           // for OfiPlugin
#include "hcl_log_manager.h"                      // for LOG_*
#include "platform/gaudi2/context_manager.h"

class HclEvent;

#define PCI_ID_STR_LEN   13
#define MAC_ADDR_STR_LEN 17

IHclDevice::IHclDevice(HclDeviceConfig& deviceConfig)
: m_deviceId(deviceConfig.m_deviceId), m_deviceConfig(deviceConfig), m_deviceType(deviceConfig.m_deviceType)
{
}

IHclDevice::~IHclDevice() noexcept(false) {}

hcclResult_t IHclDevice::destroy(bool force)
{
    LOG_HCL_TRACE(HCL, "interface force({})", force);

    if (m_ofiPlugin != nullptr)
    {
        delete m_ofiPlugin;
        m_ofiPlugin = nullptr;
    }
    return hcclSuccess;
}

hcclResult_t IHclDevice::destroyComm(HCL_Comm comm, bool force)
{
    LOG_HCL_TRACE(HCL, "interface force({})", force);
    return hcclSuccess;
}

hcclResult_t IHclDevice::onNewCommStart(HCL_Comm comm, uint32_t commSize, HclConfig& config)
{
    return hcclSuccess;
}

hcclResult_t IHclDevice::sync(HCL_Comm comm, uint16_t tag)
{
    return hcclSuccess;
}

void IHclDevice::setHal(hcl::HalPtr ptr)
{
    m_hal = ptr;
}

HCL_Rank IHclDevice::getMyRank(HCL_Comm comm)
{
    return getComm(comm).getMyRank();
}

const UniqueSortedVector& IHclDevice::getRanks(HCL_Comm comm)
{
    return getComm(comm).getRanks();
}

HCL_Rank IHclDevice::getCommRank(HCL_Comm comm)
{
    return getMyRank(comm);
}

HCL_Rank IHclDevice::getCommRank(HCL_Comm comm, HCL_Rank rank)
{
    return rank;
}

HCL_Rank IHclDevice::getGlobalRankForComm(HCL_Comm comm, HCL_Rank rankID) const
{
    return rankID;
}

HclDynamicCommunicator& IHclDevice::getComm(HCL_Comm comm)
{
    return m_dynamicComms.getComm(comm);
}

int IHclDevice::getCommSize(HCL_Comm comm)
{
    return getRanks(comm).size();
}

const UniqueSortedVector& IHclDevice::getCommRanks(HCL_Comm comm)
{
    return getRanks(comm);
}

bool IHclDevice::isCommExist(HCL_Comm comm)
{
    return m_dynamicComms.isCommExist(comm);
}

void IHclDevice::readMacInfoDriver()
{
    hlthunk_mac_addr_info kmdMacList;
    int rc = hlthunk_get_mac_addr_info(getFd(), &kmdMacList);
    VERIFY( rc == 0, "hlthunk_get_mac_addr_info() failed: {}", rc);
    m_hclNic.mask = kmdMacList.mask[0];
    for (auto nic : m_hclNic.mask)
    {
        m_hclNic.macs[nic] = kmdMacList.array[nic].addr;
    }
}

void IHclDevice::getMacInfo()
{
    const char*   macAddrInfoFilePath = (GCFG_HCL_MAC_INFO_FILE.value()).c_str();
    std::ifstream macAddrInfoFile(macAddrInfoFilePath);

    if (macAddrInfoFile.good())
    {
        VERIFY(readMacInfoFromFile(macAddrInfoFilePath) == true);
    }
    else
    {
        readMacInfoDriver();
    }

    LOG_HCL_DEBUG(HCL, "nics mask = {}", m_hclNic.mask.to_str());
}

bool IHclDevice::readMacInfoFromFile(const char* macAddrInfoFilePath)
{
    json               macAddrInfo;
    std::ifstream      macAddrInfoFile(macAddrInfoFilePath);

    try
    {
        LOG_HCL_INFO(HCL, "Loading Mac Addr info file at {}.", macAddrInfoFilePath);
        macAddrInfoFile >> macAddrInfo;

        if (macAddrInfo.find("MAC_ADDR_INFO") == macAddrInfo.end())
        {
            LOG_HCL_ERR(HCL, "Invalid Mac Addr Info File: MAC_ADDR_INFO key not found at {}.", macAddrInfoFilePath);
            return false;
        }
    }
    catch (std::exception& e)
    {
        LOG_HCL_ERR(HCL, "Invalid json file {}, error {}", macAddrInfoFilePath, e.what());
        return false;
    }

    char myPCIId[PCI_ID_STR_LEN];
    int rc = hlthunk_get_pci_bus_id_from_fd(getFd(), myPCIId, sizeof(myPCIId));
    VERIFY(rc == 0, "hlthunk_get_pci_bus_id_from_fd() failed: {}", rc);
    bool isMyPCIId = false;

    nics_mask_t mask;
    auto allMacInfo = macAddrInfo["MAC_ADDR_INFO"].get<std::vector<json>>();
    for (auto& PCIIdMacInfo : allMacInfo)
    {
        if (PCIIdMacInfo.find("PCI_ID") == PCIIdMacInfo.end())
        {
            LOG_HCL_ERR(HCL, "Invalid Mac Addr Info File: PCI_ID key not found at {}.", macAddrInfoFilePath);
            return false;
        }
        std::string PCIId = PCIIdMacInfo["PCI_ID"].get<std::string>();

        if (strcmp(myPCIId, PCIId.c_str()) != 0)
        {
            continue;
        }
        isMyPCIId = true;

        if (PCIIdMacInfo.find("MAC_ADDR_LIST") == PCIIdMacInfo.end())
        {
            LOG_HCL_ERR(HCL, "Invalid Mac Addr Info File: MAC_ADDR_LIST key not found at {}.", macAddrInfoFilePath);
            return false;
        }

        auto macList = PCIIdMacInfo["MAC_ADDR_LIST"].get<std::vector<json>>();
        unsigned port    = 0;
        for (auto& macAddr : macList)
        {
            std::string macAddrStr = macAddr.get<std::string>();
            if (!macAddrStr.empty())
            {
                if (macAddrStr.size() != MAC_ADDR_STR_LEN)
                {
                    LOG_HCL_ERR(HCL,
                                "Invalid Mac Addr Info File: MAC addr {} is of invalid length at {}.",
                                macAddrStr,
                                macAddrInfoFilePath);
                    return false;
                }

                m_hclNic.macs[port] = parseMac(macAddrStr);
                m_hclNic.mask[port] = true;
            }
            port++;
        }

        if (port != m_hal->getMaxNics())
        {
            LOG_HCL_ERR(HCL,
                        "Invalid Mac Addr Info File: invalid number of ports for PCI_ID {} at {}.",
                        myPCIId,
                        macAddrInfoFilePath);
            return false;
        }
    }

    if (!isMyPCIId)
    {
        LOG_HCL_ERR(HCL, "Invalid Mac Addr Info File: PCI ID {} not present in {}.", myPCIId, macAddrInfoFilePath);
        return false;
    }

    return true;
}

void IHclDevice::initNicsMask()
{
    getMacInfo();

    m_hclNic.mask &= ~m_deviceConfig.m_disabledPorts;

    // Get mac and IP address from all available ports and store Gaudi`s ports
    LOG_HCL_DEBUG(HCL, "disabled ports={:24b} m_nicsStatusMask={:24b}", (uint64_t)m_deviceConfig.m_disabledPorts,  (uint64_t)m_hclNic.mask);

    hcclResult_t res = updateNicsState();
    if (res != hcclSuccess)
    {
        LOG_HCL_ERR(HCL, "At least one NIC is DOWN - Please check if network interfaces are up");
    }
}

void IHclDevice::fillMacAddresses(HCL_Comm comm)
{
    LOG_HCL_DEBUG(HCL, "Filling MAC addresses");

    // Get mac and IP address from all available ports and store Gaudi`s ports
    for (auto nic : m_hclNic.mask)
    {
        getComm(comm).m_rankInfo.device.gaudiNicAddresses.nics[nic].mac.u64 = m_hclNic.macs[nic];

        // check gaudinet configuration and update if available
        auto macAddr = getComm(comm).m_rankInfo.device.gaudiNicAddresses.nics[nic].mac.u64;
        auto findItr = m_deviceConfig.m_gaudiNet.find(macAddr);
        if (findItr != m_deviceConfig.m_gaudiNet.end())
        {
            uint32_t ip = findItr->second.ipAddress;
            LOG_HCL_INFO(HCL,
                         "Found Gaudi Net Info: NIC MAC 0x{:x} => NIC IP '{}' old IP '{}'",
                         macAddr,
                         ip2str(ip),
                         ip2str(getComm(comm).m_rankInfo.device.gaudiNicAddresses.nics[nic].ip));

            getComm(comm).m_rankInfo.device.gaudiNicAddresses.nics[nic].ip = ip;
        }
        else
        {
            getComm(comm).m_rankInfo.device.gaudiNicAddresses.nics[nic].ip = 0;
        }
    }
}

hcclResult_t IHclDevice::networkFlush(HCL_Request* phRequest, synStreamHandle streamHandle)
{
    // normal (non-Gaudi) implementation are empty.
    *phRequest = HCL_Request();
    return hcclSuccess;
}

int IHclDevice::pcieFlush()
{
    int      status = 0;
    uint32_t flush_buf;
    status = hlthunk_device_memory_read_block_experimental(getFd(), &flush_buf, getHal()->getFlushPCIeReg(), 4, 0);
    if (status != 0)
    {
        LOG_HCL_DEBUG(HCL, "pcieFlush operation failed with status: [{}]", status);
        return hcclInternalError;
    }
    LOG_HCL_DEBUG(HCL, "pcieFlush operation was completed");
    return status;
}

HclDeviceConfig& IHclDevice::getDeviceConfig()
{
    return m_deviceConfig;
}

int IHclDevice::getFd() const
{
    return m_deviceConfig.m_fd;
}

bool IHclDevice::isNicUp(uint32_t nic)
{
    hlthunk_get_habana_link_state_in  in_port = {nic, 0};
    hlthunk_get_habana_link_state_out state   = {0};
    int                               hl_res  = hlthunk_get_habana_link_state(getFd(), &in_port, &state);
    if (hl_res != 0)
    {
        LOG_HCL_DEBUG(HCL, "Failed to query link state - check driver status");
    }
    if (hl_res == 0 && state.up == 1)
    {
        return true;
    }
    return false;
}

hcclResult_t IHclDevice::updateNicsState()
{
    // check link status for all ports,
    // return after checking all ports even if detected error
    hcclResult_t  res        = hcclSuccess;
    HclConfigType configType = (HclConfigType)GCFG_BOX_TYPE_ID.value();
    for (uint32_t nic = 0; nic < m_hal->getMaxNics(); nic++)
    {
        bool up  = isNicUp(nic);
        // DEFAULT_SPOTLIGHT is used since:
        // 1. At this point at time we do not know which spotlight communicator will be used
        // 2. This method is for verification only
        bool ext = isScaleOutPort(nic);

        // disabled port, just log
        if ((!m_hclNic.mask[nic]))
        {
            LOG_HCL_TRACE(HCL,
                          "{} Network link fd({}), {} port({}) DISABLED, is {}",
                          GCFG_BOX_TYPE.value(),
                          getFd(),
                          ext ? "external" : "internal",
                          nic,
                          up ? "up" : "down");
        }
        else  // enabled port
        {
            // state up
            if (up)
            {
                LOG_HCL_TRACE(HCL,
                              "{} Network link fd({}), {} port({}) is up",
                              GCFG_BOX_TYPE.value(),
                              getFd(),
                              ext ? "external" : "internal",
                              nic);
            }
            // state down :(
            else
            {
                // clear bit
                m_hclNic.mask.clear(nic);

                // unknown server type could be simulator or PCIe card
                bool unknownServerType = (configType == BACK_2_BACK || configType == UNKNOWN);
                // internal port - we can't continue
                if (!ext && !unknownServerType)
                {
                    LOG_HCL_ERR(HCL,
                                "{} Network link fd({}), {} port({}) is down",
                                GCFG_BOX_TYPE.value(),
                                getFd(),
                                ext ? "external" : "internal",
                                nic);
                    res = hcclInternalError;
                }
                // HLS1 external port or unknown server type - issue warning
                // if will try to open QP on port later it will ERROR
                else
                {
                    LOG_HCL_DEBUG(HCL,
                                 "{} Network link fd({}), external port({}) is down",
                                 GCFG_BOX_TYPE.value(),
                                 getFd(),
                                 nic);
                }
            }
        }
    }
    return res;
}

void IHclDevice::waitForAllEvents(bool isCsDone) {}

void IHclDevice::waitForAllEvents(uint32_t queueOffset, bool isCsDone) {}

uint32_t IHclDevice::createQp(uint32_t port, uint8_t qpId)
{
    uint32_t qpn = 0;
    int rc = hlthunk_alloc_conn(getFd(), port, &qpn);
    VERIFY(rc == 0,
           "Failed to allocate QP, hlthunk_alloc_conn() failed({}), device may be out of QPs, or network "
           "interfaces are not up",
           rc);

    return qpn;
}

uint32_t IHclDevice::allocateConnection(uint32_t port, HCL_Rank rank, HCL_Comm comm, uint8_t qpId, uint8_t qpSet)
{
    VERIFY(isNicUp(port), "Nic({}) is DOWN, can't allocate connection", port);

    uint32_t& qpn = getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs[port].qp[qpSet][qpId];

    qpn = createQp(port, qpId);

    LOG_HCL_DEBUG(HCL,
                  "Allocate QP, remoteRank({}){} nic: {} qpSet: {}, Qpn: {}, qpIdx: {}",
                  rank,
                  getMyRank(comm) == rank ? " Loopback connection, " : "",
                  port,
                  qpSet,
                  qpn,
                  qpId);

    return qpn;
}

void IHclDevice::destroyQp(uint32_t port, uint32_t qpn)
{
    int rc = hlthunk_destroy_conn(getFd(), port, qpn);
    VERIFY(rc == 0, "hlthunk_destroy_conn() failed: {}", rc);
}

void IHclDevice::closeQps(HCL_Comm comm, const UniqueSortedVector& ranks)
{
    for (auto& rank : ranks)
    {
        // don't call getActiveNics on same rank
        if (rank == getMyRank(comm))
        {
            continue;
        }

        // don't use getActiveNics so loopback mode also works
        // access GaudiNicQPs by index and translate to port
        for (uint8_t index = 0; index < COMPACT_RANK_INFO_NICS; index++)
        {
            for (uint8_t qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
            {
                for (unsigned i = 0; i < m_hal->getMaxQPsPerNic(); i++)
                {
                    const uint32_t qpn = getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs.qp[index].qp[qpSet][i];
                    if (qpn == 0) continue;

                    uint8_t port = getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs.qp[index].nic;

                    LOG_HCL_DEBUG(HCL,
                                  "destroy QP for rank({}) comm({}) port({}) Qp: {}",
                                  rank,
                                  comm,
                                  port,
                                  getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs[port].qp[qpSet][i]);

                    destroyQp(port, getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs[port].qp[qpSet][i]);

                    getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs[port].qp[qpSet][i] = 0;
                }
            }
        }
    }
}

void IHclDevice::deleteCommConnections(HCL_Comm comm)
{
    closeQps(comm, getRanks(comm));
}

void IHclDevice::registerOpenQpCallback(HclConfigType configType, std::function<hcclResult_t(HCL_Comm)> callback)
{
    m_openQpsCallbacks[configType] = callback;
}

hcclResult_t IHclDevice::openQps(HCL_Comm comm)
{
    hcclResult_t  rc;
    HclConfigType configType = (HclConfigType)GCFG_BOX_TYPE_ID.value();

    LOG_HCL_DEBUG(HCL, "Config type ({}), null-submit ({})", configType, GCFG_HCL_NULL_SUBMIT.value());

    if (m_openQpsCallbacks.count(configType) == 0)
    {
        LOG_HCL_ERR(HCL, "Unknown config type ({})", configType);
        return hcclInvalidArgument;
    }
    rc = m_openQpsCallbacks[configType](comm);

    return rc;
}

void IHclDevice::openWQs()
{
    VERIFY(m_hal);

    for (auto nic : m_hclNic.mask)
    {
        // SCALEOUT_SPOTLIGHT is used since we need to allocate all scaleout WQs
        // (both static scaleout ports and hybrid ports), in case
        // a hybrid port will be used as a scaleout port at some point
        uint32_t max_qps =
            isScaleOutPort(nic, SCALEOUT_SPOTLIGHT) ? m_hal->getMaxQpPerExternalNic() : m_hal->getMaxQpPerInternalNic();

        m_hclNic[nic] = allocateNic(nic, max_qps + 1);

        m_hclNic[nic]->init();
    }
}

uint8_t IHclDevice::getPeerNic(HCL_Rank rank, HCL_Comm comm, uint8_t port)
{
    return port;
}

hcclResult_t IHclDevice::updateQps(HCL_Comm comm)
{
    LOG_HCL_HEADER(HCL);

    for (auto& rank : getRanks(comm))
    {
        updateRankQps(comm, rank);
    }
    return hcclSuccess;
}

static inline void convertMacAddress(uint8_t* out, uint64_t mac)
{
    uint8_t* in = (uint8_t*)&mac;
    std::reverse_copy(in, in + 6, out);
}

hcclResult_t
IHclDevice::setupQps(HCL_Comm comm, HCL_Rank rank, uint32_t stream, uint32_t port, uint32_t qpn, uint8_t qpSet)
{
    uint16_t peerNic = getPeerNic(rank, comm, port);

    LOG_HCL_TRACE(HCL,
                  "comm({}), Remote Rank({}), stream({}), port({}), peer nic({}), qpn({}), qpset({})",
                  comm,
                  rank,
                  stream,
                  port,
                  peerNic,
                  qpn,
                  qpSet);

    GaudiNicAddress remoteNicAddress =
        getComm(comm).m_remoteDevices[rank]->device.gaudiNicAddresses.nics[peerNic];

    GaudiNicQPs::NicQPs remoteNicQPs =
        getComm(comm).m_remoteDevices[rank]->remoteInfo.gaudiNicQPs[peerNic];

    struct hlthunk_requester_conn_ctx req_ctx = {};

    uint64_t remoteMac = remoteNicAddress.mac.u64;
    uint64_t myMac  = getComm(comm).m_rankInfo.device.gaudiNicAddresses.nics[port].mac.u64;
    auto        findItr   = m_deviceConfig.m_gaudiNet.find(myMac);
    LOG_HCL_DEBUG(HCL, "Rank({}), myMac=0x{:x},  remoteMac=0x{:x}", rank, myMac, remoteMac);
    // If we have IP info, and the destination IP is in a different subnet, use gateway MAC address
    if (findItr != m_deviceConfig.m_gaudiNet.end())
    {
        uint32_t srcIp   = getComm(comm).m_rankInfo.device.gaudiNicAddresses.nics[port].ip;
        uint32_t destIp  = remoteNicAddress.ip;
        uint32_t netMask = findItr->second.subnetMask;
        if ((destIp & netMask) != (srcIp & netMask))
        {
            remoteMac = findItr->second.gatewayMacAddress;
            LOG_HCL_DEBUG(HCL,
                          "Set gateway MAC to reach destination: Dest IP '{}' => GW MAC '0x{:x}'",
                          ip2str(destIp),
                          remoteMac);
        }
    }

    req_ctx.dst_ip_addr       = ntohl(remoteNicAddress.ip);
    req_ctx.dst_conn_id       = remoteNicQPs.qp[qpSet][stream];
    req_ctx.priority          = GCFG_REQUESTER_PRIORITY.value();
    req_ctx.timer_granularity = 0;
    req_ctx.swq_granularity   = 0;
    req_ctx.congestion_wnd    = GCFG_CONGESTION_WINDOW.value();
    convertMacAddress(req_ctx.dst_mac_addr, remoteMac);

    updateRequesterContext(req_ctx, comm, port, rank, qpn, qpSet);

    struct hlthunk_responder_conn_ctx res_ctx = {};

    res_ctx.dst_ip_addr = ntohl(remoteNicAddress.ip);
    res_ctx.dst_conn_id = remoteNicQPs.qp[qpSet][stream];
    res_ctx.priority    = GCFG_RESPONDER_PRIORITY.value();
    convertMacAddress(res_ctx.dst_mac_addr, remoteMac);

    updateResponderContext(res_ctx, comm, port, rank, qpn, qpSet);

    LOG_HCL_TRACE(HCL, "qp:{}->{} nic:{} ip:{} mac:0x{:x}", qpn, res_ctx.dst_conn_id, port, ip2str(remoteNicAddress.ip), remoteMac);

    int hlthunk_res = hlthunk_set_responder_conn_ctx(getFd(), port, qpn, &res_ctx);
    if (hlthunk_res != 0)
    {
        LOG_HCL_ERR(HCL, "hlthunk failed to set responder connection context ({})", hlthunk_res);
        return hcclInternalError;
    }

    struct hlthunk_requester_conn_ctx_out req_ctx_out = {};
    hlthunk_res = hlthunk_set_requester_conn_ctx(getFd(), port, qpn, &req_ctx, &req_ctx_out);
    if (hlthunk_res != 0)
    {
        LOG_HCL_ERR(HCL, "hlthunk failed to set requester connection context ({})", hlthunk_res);
        return hcclInternalError;
    }

    return hcclSuccess;
}

hcclResult_t IHclDevice::updateRankQps(HCL_Comm comm, HCL_Rank rank)
{
    LOG_HCL_TRACE(HCL,
                  "updateRankQps remoteRank({}) maxNics {} MaxQPsPerNic {}",
                  rank,
                  m_hal->getMaxNics(),
                  m_hal->getMaxQPsPerNic());
    HclConfigType configType = (HclConfigType)GCFG_BOX_TYPE_ID.value();

    // don't use getActiveNics so loopback mode also works
    // access GaudiNicQPs by index and translate to port
    LOG_HCL_INFO(HCL_COORD, "Rank comm({}) rank({}) start", comm, rank);
    uint32_t opened_qps = 0;
    for (uint8_t index = 0; index < COMPACT_RANK_INFO_NICS; index++)
    {
        for (uint8_t qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
        {
            for (uint8_t stream = 0; stream < m_hal->getMaxQPsPerNic(); stream++)
            {
                const uint32_t qpn = getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs.qp[index].qp[qpSet][stream];

                if (qpn == 0) continue;  // Connection wasn't opened.
                if (rank == getMyRank(comm) && !(configType == LOOPBACK)) continue;

                LOG_HCL_DEBUG(HCL_COORD,
                              "comm({}), rank({}), stream({}), port({}), qpn({}), qpSet({}) calling setupQps",
                              comm,
                              rank,
                              stream,
                              getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs.qp[index].nic,
                              qpn,
                              qpSet);

                // translate the index to nic
                hcclResult_t rs = setupQps(comm,
                                           rank,
                                           stream,
                                           getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs.qp[index].nic,
                                           qpn,
                                           qpSet);
                if (rs != hcclSuccess)
                {
                    return rs;
                }
                opened_qps++;
            }
        }
    }
    LOG_HCL_INFO(HCL_COORD, "Rank comm({}) rank({}) done, opened({}) QPs", comm, rank, opened_qps);
    return hcclSuccess;
}

void IHclDevice::updateRequesterContext(hlthunk_requester_conn_ctx& req_ctx,
                                        HCL_Comm                    comm,
                                        uint8_t                     nic,
                                        HCL_Rank                    remoteRank,
                                        uint32_t                    qpn,
                                        uint8_t                     qpSet)
{
}

void IHclDevice::updateResponderContext(hlthunk_responder_conn_ctx& res_ctx,
                                        HCL_Comm                    comm,
                                        uint8_t                     nic,
                                        HCL_Rank                    remoteRank,
                                        uint32_t                    qpn,
                                        uint8_t                     qpSet)
{
}

void IHclDevice::getInnerRanks(const HCL_Comm comm, UniqueSortedVector& innerRanks)
{
    auto& comm_ref = getComm(comm);
    innerRanks.insert_range_sorted(comm_ref.getInnerRanksExclusive().begin(), comm_ref.getInnerRanksExclusive().end());
}

void IHclDevice::getOuterRanks(const HCL_Comm comm, UniqueSortedVector& outerRanks)
{
    auto& comm_ref = getComm(comm);
    outerRanks.insert_range_sorted(comm_ref.getOuterRanksExclusive().begin(), comm_ref.getOuterRanksExclusive().end());
}

void IHclDevice::getPeerRanks(const HCL_Comm comm, UniqueSortedVector& syncRanks)
{
    getInnerRanks(comm, syncRanks);
    getOuterRanks(comm, syncRanks);
}

bool IHclDevice::isCommunicatorInPod(HCL_Comm comm)
{
    return getComm(comm).isCommunicatorInPod();
}

bool IHclDevice::isCommunicatorPodPeers(HCL_Comm comm)
{
    return getComm(comm).isCommunicatorPodPeers();
}

bool IHclDevice::isCommunicatorHierarchical(HCL_Comm comm)
{
    return getComm(comm).isCommunicatorHierarchical();
}

hcclResult_t IHclDevice::prepareAndValidateCommunicator(HCL_Comm comm, bool isLoopbackModeOrNullSubmission)
{
    LOG_DEBUG(HCL, "Validating comm ({})", comm);
    return getComm(comm).prepareAndValidateComm(isLoopbackModeOrNullSubmission);
}

HCL_Comm IHclDevice::allocateNewComm()
{
    return m_dynamicComms.createNextComm(m_hal);
}

HCL_Comm IHclDevice::allocateCommWorld()
{
    if (!m_dynamicComms.createHclCommWorld(m_hal))
    {
        LOG_ERR(HCL, "Was not able to allocate HCL_COMM_WORLD comm ID");
        return HCL_INVALID_COMM;
    }
    return HCL_COMM_WORLD;
}

int IHclDevice::getNumActiveComms() const
{
    return m_dynamicComms.getNumOfActiveComms();
}

int IHclDevice::getPodSize(HCL_Comm comm)
{
    return getComm(comm).getPodSize();
}

nics_mask_t IHclDevice::getNicsStatusMask() const
{
    return m_hclNic.mask;
}

ofi_t* IHclDevice::getOfiHandle()
{
    if (m_ofiPlugin == nullptr)
    {
        return nullptr;
    }
    else
    {
        return m_ofiPlugin->p_ofi;
    }
}

void IHclDevice::createOfiPlugin()
{
    if (GCFG_HCCL_OVER_OFI.value())
    {
        m_ofiPlugin    = new OfiPlugin(getFd(), getHwModuleId());
        m_ofiComponent = m_ofiPlugin->p_ofi->getOfiComponent(getOfiDeviceId());
    }
}

void IHclDevice::setScaleoutMode(const int scaleOutGNICs)
{
    if (GCFG_HCCL_GAUDI_DIRECT.isSetFromUserConfig() && !GCFG_HCCL_OVER_OFI.isSetFromUserConfig())
    {
        LOG_HCL_ERR(HCL,
                    "Conflicting scale-out environment settings. HCCL_GAUDI_DIRECT cannot be defined unless "
                    "HCCL_OVER_OFI is defined.");
        std::terminate();
    }

    if (GCFG_HCCL_OVER_OFI.value())
    {
        LOG_HCL_INFO(HCL, "ofi selected by user");
    }

    // Start auto-detection flow - if scaleout mode wasn't specified by the user, identify it
    // First, check whether GNICs are available for scale-out
    else if (scaleOutGNICs)
    {
        LOG_HCL_INFO(HCL, "gaudi nics selected");
    }
    // Second, check whether OFI is available for scale-out, only if the user didn't request not to use it
    // (HCCL_OVER_OFI=0)
    else if (OfiPlugin::initializeOFIPluginIfNeeded() &&
             !((GCFG_HCCL_OVER_OFI.value() == 0) && GCFG_HCCL_OVER_OFI.isSetFromUserConfig()))
    {
        GCFG_HCCL_OVER_OFI.setValue(true);
        LOG_HCL_INFO(HCL, "ofi selected");
        setGaudiDirect();
    }
    // If non of the above matched, no scale-out mode was detected
    else
    {
        LOG_HCL_INFO(HCL, "No suitable scale-out method detected. Scale-out is not available");
        m_scaleoutAvailable = false;
    }

    // Check if GCFG_HCCL_GAUDI_DIRECT is set (by auto-detect / user)
    // if so, enable AWS environment varialbe for RDMA: FI_EFA_USE_DEVICE_RDMA
    // and disable sending inline data in Mellanox environment: MLX5_SCATTER_TO_CQE
    if (GCFG_HCCL_GAUDI_DIRECT.value())
    {
        // Set FI_EFA_USE_DEVICE_RDMA without overwriting value if was already set
        LOG_HCL_DEBUG(HCL, "Setting FI_EFA_USE_DEVICE_RDMA without overwrite.");
        setenv("FI_EFA_USE_DEVICE_RDMA", "1", 0);
        // when using mpi, param configuration might not succeed- indicate the situation to the user
        int enableInline = getEnvInt("MLX5_SCATTER_TO_CQE", -1);
        if (enableInline != 0)
        {
            LOG_HCL_WARN(
                HCL,
                "MLX5_SCATTER_TO_CQE={}, to avoid sending inline data, attempting to set it to MLX5_SCATTER_TO_CQE=0",
                (enableInline == -1) ? "<not set>" : std::to_string(enableInline));
        }
        setenv("MLX5_SCATTER_TO_CQE", "0", 0);
    }
}

int IHclDevice::getHwModuleId()
{
    return m_deviceConfig.getHwModuleId();
}

int IHclDevice::getOfiDeviceId()
{
    if (m_ofiDeviceID < 0)
    {
        // the ofi component is selected based on the HW module ID
        m_ofiDeviceID = getOfiHandle()->getOFIDevice();
    }
    return m_ofiDeviceID;
}

bool IHclDevice::isACcbHalfFullForDeviceBenchMark(const unsigned archStreamIdx)
{
    VERIFY(IS_DEVICE_GEN2ARCH(getDeviceType()), "Invalid device type '{}'.", getDeviceType());
    return false;
};