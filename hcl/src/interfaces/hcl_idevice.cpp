#include "interfaces/hcl_idevice.h"

#include <cstring>  // for memset, memcpy, NULL
#include <array>    // for array
#include <cstdint>  // for uint32_t, uint8_t
#include <memory>   // for __shared_ptr_access
#include <set>      // for set
#include <string>   // for string
#include <utility>  // for pair

#include "hlthunk.h"                                      // for hlthunk_device_name, hlthunk_...
#include "hcl_api_types.h"                                // for HCL_Comm, HCL_Rank
#include "hcl_config.h"                                   // for HclConfig
#include "platform/gen2_arch_common/hcl_device_config.h"  // for HclDeviceConfig
#include "hcl_dynamic_comms_manager.h"                    // for HclDynamicCommsManager
#include "hcl_dynamic_communicator.h"                     // for HclDynamicCommunicator
#include "hcl_global_conf.h"                              // for GlobalConfImpl::value
#include "hcl_nic.h"                                      // for HclNic
#include "interfaces/hcl_remote_device.h"                 // for HclRemoteDevice
#include "hcl_utils.h"                                    // for macAddr2Str, VERIFY
#include "interfaces/hcl_hal.h"                           // for HalPtr, Hal
#include "interfaces/hcl_unique_sorted_vector.h"          // for UniqueSortedVector
#include "libfabric/hl_ofi.h"                             // for ofi_t
#include "ofi_plugin.h"                                   // for OfiPlugin
#include "hcl_log_manager.h"                              // for LOG_*
#include "hcl_types.h"                                    // for MAX_COMPACT_RANK_INFO_NICS, SYN_VALID_DEVICE_ID
#include "ibverbs/hcl_ibverbs.h"                          // for g_ibv
#include "fault_tolerance_inc.h"                          // for HLFT.* macros

#include "platform/gen2_arch_common/server_connectivity_user_config.h"  // for json

#include <netpacket/packet.h>  // for sockaddr_ll
#include <linux/ethtool.h>     // for ethtool_drvinfo, ETHTOOL_GDRVINFO
#include <ifaddrs.h>           // for ifaddrs, freeifaddrs, getifa...
#include <sys/ioctl.h>         // for ioctl
#include <linux/sockios.h>     // for SIOCETHTOOL
#include <net/if.h>            // for

class HclEvent;

#define MAC_ADDR_STR_LEN 17

static inline void convertMacAddress(uint8_t* out, const uint64_t mac)
{
    const uint8_t* in = (const uint8_t*)(&mac);
    std::reverse_copy(in, in + 6, out);
}

IHclDevice::IHclDevice(HclDeviceConfig& deviceConfig)
: m_deviceAcquired(deviceConfig.isDeviceAcquired()), m_deviceConfig(deviceConfig)
{
    LOG_HCL_DEBUG(HCL, "ctor, m_deviceAcquired={}, deviceType={}", m_deviceAcquired, deviceConfig.getDeviceTypeStr());
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

hcclResult_t IHclDevice::destroyComm([[maybe_unused]] HCL_Comm comm, bool force)
{
    LOG_HCL_TRACE(HCL, "interface force({})", force);
    return hcclSuccess;
}

hcclResult_t IHclDevice::onNewCommStart([[maybe_unused]] HCL_Comm   comm,
                                        [[maybe_unused]] uint32_t   commSize,
                                        [[maybe_unused]] HclConfig& config)
{
    return hcclSuccess;
}

hcclResult_t IHclDevice::sync([[maybe_unused]] HCL_Comm comm, [[maybe_unused]] uint16_t tag)
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

HCL_Rank IHclDevice::getCommRank([[maybe_unused]] HCL_Comm comm, HCL_Rank rank)
{
    return rank;
}

HCL_Rank IHclDevice::getGlobalRankForComm([[maybe_unused]] HCL_Comm comm, HCL_Rank rankID) const
{
    return rankID;
}

HclDynamicCommunicator& IHclDevice::getComm(HCL_Comm comm)
{
    return m_dynamicComms.getComm(comm);
}

uint32_t IHclDevice::getCommSize(HCL_Comm comm)
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

size_t IHclDevice::getMaxCommNum() const
{
    return m_dynamicComms.getMaxCommNum();
}

void IHclDevice::getMacAddressInfo()
{
    portMaskConfig portsMasks;
    g_ibv.get_port_mask(portsMasks);
    LOG_HCL_DEBUG(HCL, "mask.ports_mask={:024b}", portsMasks.hwPortsMask);

    const uint64_t enabledNicsMask = portsMasks.hwPortsMask;

    /* All ports are disabled */
    if (enabledNicsMask == 0)
    {
        m_hclNic.mask = 0;
        return;
    }

    const char* myPciId = m_deviceConfig.getDevicePciBusId();

    struct ifaddrs* ifaddr;
    VERIFY(getifaddrs(&ifaddr) == 0, "Unable to retrieve network interfaces");

    struct ethtool_drvinfo drvinfo;
    struct ifreq           ifr;
    ifr.ifr_data = (char*)&drvinfo;
    drvinfo.cmd  = ETHTOOL_GDRVINFO;

    uint64_t nicsMacMask = 0;
    for (auto ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        const std::string name = (ifa)->ifa_name;
        /* Discard interfaces other than AF_PACKET. */
        if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_PACKET) continue;

        strcpy(ifr.ifr_name, name.c_str());

        const int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock == -1)
        {
            freeifaddrs(ifaddr);
            VERIFY(false, "Failed opening socket for driver info");
        }

        /* skip interfaces with no ethtool support */
        if (ioctl(sock, SIOCETHTOOL, &ifr) < 0)
        {
            close(sock);
            continue;
        }

        close(sock);

        std::string net_if_drv(drvinfo.driver);

        /* skip interfaces of other vendors */
        std::string habDrv("habanalabs");
        if (net_if_drv.find(habDrv.c_str()) == std::string::npos) continue;

        /* skip interfaces of other devices */
        if (strstr(drvinfo.bus_info, myPciId) != drvinfo.bus_info) continue;

        /* read the interface port */
        const std::string filePath = "/sys/class/net/" + name + "/dev_port";
        std::ifstream     file(filePath);

        if (!file.is_open())
        {
            freeifaddrs(ifaddr);
            VERIFY(false, "Failed to open file: {}", filePath);
        }

        std::string devicePort;
        std::getline(file, devicePort);

        if (file.fail())
        {
            freeifaddrs(ifaddr);
            VERIFY(false, "Failed to read value from file: {}", filePath);
        }

        file.close();
        uint64_t devPortNum = stol(devicePort);

        /* Update MAC only for active NICs */
        const uint64_t nicMask = 1ULL << devPortNum;
        if (enabledNicsMask && nicMask)
        {
            nicsMacMask |= nicMask;
            m_hclNic.macs[devPortNum] = ((struct sockaddr_ll*)ifa->ifa_addr)->sll_addr;
        }
    }

    freeifaddrs(ifaddr);

    uint64_t bcastMac = std::stoull("ffffffffffff", nullptr, 16);
    for (uint64_t port = 0; port < 64; port++)
    {
        const uint64_t nicMask = 1ULL << port;
        /* Update internal active NICs */
        if ((enabledNicsMask & nicMask) && !(nicsMacMask & nicMask))
        {
            nicsMacMask |= nicMask;
            /* The MAC address of internal ports is irrelevant, so use broadcast */
            m_hclNic.macs[port] = &bcastMac;
        }
    }

    m_hclNic.mask = nicsMacMask;
}

void IHclDevice::readMacInfoDriver()
{
    hlthunk_mac_addr_info kmdMacList;
    int                   rc = hlthunk_get_mac_addr_info(getFd(), &kmdMacList);
    VERIFY(rc == 0, "hlthunk_get_mac_addr_info() failed: {}", rc);
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
        if (GCFG_HCCL_GET_MACS_FROM_DRIVER.value())
        {
            readMacInfoDriver();
        }
        else
        {
            getMacAddressInfo();
        }
    }

    LOG_HCL_DEBUG(HCL, "nics mask = {}", m_hclNic.mask.to_str());
    for (const auto nic : m_hclNic.mask)
    {
        const uint64_t mac_addr          = m_hclNic.macs[nic];
        uint64_t       mac_addr_reversed = 0;
        convertMacAddress((uint8_t*)(&mac_addr_reversed), mac_addr);

        LOG_HCL_DEBUG(HCL, "nic[{}]=0x{:x}", nic, mac_addr_reversed);
    }
}

bool IHclDevice::readMacInfoFromFile(const char* macAddrInfoFilePath)
{
    json          macAddrInfo;
    std::ifstream macAddrInfoFile(macAddrInfoFilePath);

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

    const char* myPciId   = m_deviceConfig.getDevicePciBusId();
    bool        isMyPCIId = false;

    nics_mask_t mask;
    auto        allMacInfo = macAddrInfo["MAC_ADDR_INFO"].get<std::vector<json>>();
    for (auto& PCIIdMacInfo : allMacInfo)
    {
        if (PCIIdMacInfo.find("PCI_ID") == PCIIdMacInfo.end())
        {
            LOG_HCL_ERR(HCL, "Invalid Mac Addr Info File: PCI_ID key not found at {}.", macAddrInfoFilePath);
            return false;
        }
        std::string PCIId = PCIIdMacInfo["PCI_ID"].get<std::string>();

        if (strcmp(myPciId, PCIId.c_str()) != 0)
        {
            continue;
        }
        isMyPCIId = true;

        if (PCIIdMacInfo.find("MAC_ADDR_LIST") == PCIIdMacInfo.end())
        {
            LOG_HCL_ERR(HCL, "Invalid Mac Addr Info File: MAC_ADDR_LIST key not found at {}.", macAddrInfoFilePath);
            return false;
        }

        auto     macList = PCIIdMacInfo["MAC_ADDR_LIST"].get<std::vector<json>>();
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
                        myPciId,
                        macAddrInfoFilePath);
            return false;
        }
    }

    if (!isMyPCIId)
    {
        LOG_HCL_ERR(HCL, "Invalid Mac Addr Info File: PCI ID {} not present in {}.", myPciId, macAddrInfoFilePath);
        return false;
    }

    return true;
}

void IHclDevice::initNicsMask()
{
    getMacInfo();

    m_hclNic.mask &= ~m_deviceConfig.getDisabledPorts();

    // Get mac and IP address from all available ports and store Gaudi`s ports
    LOG_HCL_DEBUG(HCL,
                  "disabled ports={:024b} m_nicsStatusMask={:024b}",
                  (uint64_t)m_deviceConfig.getDisabledPorts(),
                  (uint64_t)m_hclNic.mask);

    const nics_mask_t shutdownSimPortsMask =
        GCFG_HCL_FAULT_TOLERANCE_LOGICAL_PORTS_SHUTDOWN_MASK.value();  // Used for CI testing only
    HLFT_DBG("shutdownSimPortsMask={:024b}", (uint64_t)shutdownSimPortsMask);
    for (uint32_t nic = 0; nic < m_hal->getMaxNics(); nic++)
    {
        const NicLkdEventsEnum event =
            isNicUp(nic) ? NicLkdEventsEnum::NIC_LKD_EVENTS_UP : NicLkdEventsEnum::NIC_LKD_EVENTS_DOWN;
        updateNicState(nic, event, true);

        // Check fault tolerance enabled - if nic marked as up but physical link is down, we need to update the failed
        // mask
        if (GCFG_HCL_FAULT_TOLERANCE_ENABLE.value() && m_hclNic.mask[nic] && isScaleOutPort(nic))
        {
            const eIbvNicPhysicalState nicPhysicalState = getNicPhysicalState(nic);
            HLFT_INF("scaleout nic {} physical status {}", nic, nicPhysicalState);
            if ((nicPhysicalState == eIbvNicPhysicalState::Shutdown) ||
                ((shutdownSimPortsMask != 0) && shutdownSimPortsMask.get(getLogicalScaleoutPortNum(nic))))
            {
                m_failedScaleOutPortsMask.set(getLogicalScaleoutPortNum(nic));
            }
        }
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
        auto findItr = m_deviceConfig.getGaudiNet().find(macAddr);
        if (findItr != m_deviceConfig.getGaudiNet().end())
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

hcclResult_t IHclDevice::networkFlush(HCL_Request* phRequest, [[maybe_unused]] synStreamHandle streamHandle)
{
    // normal (non-Gaudi) implementation are empty.
    *phRequest = HCL_Request();
    return hcclSuccess;
}

int IHclDevice::getFd() const
{
    return m_deviceConfig.getFd();
}

bool IHclDevice::isNicUp([[maybe_unused]] uint32_t nic)
{
    return false;
}

void IHclDevice::updateNicState(const uint32_t nic, const NicLkdEventsEnum event, const bool atInit)
{
    const bool isPortUp = (event == NicLkdEventsEnum::NIC_LKD_EVENTS_UP);
    // update state of enabled ports
    if ((m_hclNic.mask[nic]))
    {
        m_hclNic.state[nic] = isPortUp;
        if (!isPortUp && atInit) m_hclNic.mask[nic] = false;  // update disabled nic mask
    }

    LOG_HCL_TRACE(HCL,
                  "{} Network link fd({}), {} port({}){} is {}, event={}",
                  GCFG_BOX_TYPE.value(),
                  getFd(),
                  isScaleOutPort(nic) ? "external" : "internal",
                  nic,
                  m_hclNic.mask[nic] ? "" : " DISABLED",
                  isPortUp ? "up" : "down",
                  (unsigned)event);
}

void IHclDevice::waitForAllEvents([[maybe_unused]] bool isCsDone) {}

void IHclDevice::waitForAllEvents([[maybe_unused]] uint32_t queueOffset, [[maybe_unused]] bool isCsDone) {}

uint32_t IHclDevice::allocateQp(uint32_t port, HCL_Rank rank, HCL_Comm comm, uint8_t qpId, uint8_t qpSet)
{
    VERIFY(isNicUp(port), "Nic({}) is DOWN, can't allocate connection", port);

    uint32_t& qpn = getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs[port].qp[qpSet][qpId];

    qpn = createQpnInLKD(port, qpId);

    LOG_HCL_DEBUG(HCL,
                  "Allocate QP, remoteRank({}){} nic: {} qpSet: {}, qpn: {}, qpIdx: {}",
                  rank,
                  getMyRank(comm) == rank ? " Loopback connection, " : "",
                  port,
                  qpSet,
                  qpn,
                  qpId);

    return qpn;
}

void IHclDevice::registerOpenQpCallback(HclConfigType configType, std::function<hcclResult_t(HCL_Comm)> callback)
{
    m_openQpsCallbacks[configType] = callback;
}

hcclResult_t IHclDevice::openQpToRemoteRanks(const HCL_Comm comm)
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
        uint32_t max_qps =
            isScaleOutPort(nic /*, HCL_Comm comm*/) ? m_hal->getMaxQpPerExternalNic() : m_hal->getMaxQpPerInternalNic();

        m_hclNic[nic] = allocateNic(nic, max_qps + 1);

        m_hclNic[nic]->init();
    }
}

uint8_t
IHclDevice::getPeerNic([[maybe_unused]] const HCL_Rank rank, [[maybe_unused]] const HCL_Comm comm, const uint8_t port)
{
    return port;
}

hcclResult_t IHclDevice::connectCommQps(HCL_Comm comm)
{
    LOG_HCL_HEADER(HCL);

    for (auto& rank : getRanks(comm))
    {
        connectRankQps(comm, rank);
    }
    return hcclSuccess;
}

hcclResult_t IHclDevice::connectRankQps(HCL_Comm comm, HCL_Rank rank)
{
    LOG_HCL_TRACE(HCL,
                  "connectRankQps remoteRank({}) maxNics {} MaxQPsPerNic {}",
                  rank,
                  m_hal->getMaxNics(),
                  m_hal->getMaxQPsPerNic());

    // don't use getActiveNics so loopback mode also works
    // access GaudiNicQPs by index and translate to port
    LOG_HCL_INFO(HCL_COORD, "Rank comm({}) rank({}) start", comm, rank);
    uint32_t opened_qps = 0;
    for (uint8_t index = 0; index < getMaxNumScaleUpPortsPerConnection(); index++)
    {
        for (uint8_t qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
        {
            for (uint8_t stream = 0; stream < m_hal->getMaxQPsPerNic(); stream++)
            {
                const uint32_t qpn = getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs.qp[index].qp[qpSet][stream];

                if (qpn == 0 || rank == getMyRank(comm)) continue;

                const uint16_t nic = getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs.qp[index].nic;
                LOG_HCL_DEBUG(HCL_COORD,
                              "comm({}), rank({}), index={}, stream({}), nic({}), qpn({}), qpSet({}) calling "
                              "establishQpConnectionWithPeerQp",
                              comm,
                              rank,
                              index,
                              stream,
                              nic,
                              qpn,
                              qpSet);

                // translate the index to nic
                hcclResult_t rs = establishQpConnectionWithPeerQp(comm, rank, stream, nic, qpn, qpSet);
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

bool IHclDevice::isCommunicatorInScaleupGroup(HCL_Comm comm)
{
    return getComm(comm).isCommunicatorInScaleupGroup();
}

bool IHclDevice::isCommunicatorScaleupGroupPeers(HCL_Comm comm)
{
    return getComm(comm).isCommunicatorScaleupGroupPeers();
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
    return m_dynamicComms.createNextComm(m_hal, getServerDef());
}

int IHclDevice::getNumActiveComms() const
{
    return m_dynamicComms.getNumOfActiveComms();
}

uint32_t IHclDevice::getScaleupGroupSize(HCL_Comm comm)
{
    return getComm(comm).getScaleupGroupSize();
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
        return m_ofiPlugin->p_ofi.get();
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

void IHclDevice::setScaleoutMode(const unsigned scaleOutGNICs)
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

    // check if stand-alone device
    else if (!g_ibv.has_ib_device())
    {
        LOG_HCL_INFO(HCL, "No IB device detected. No scale-up or scale-out ports");
        m_scaleoutAvailable = false;
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

    VERIFY(
        !(GCFG_HCCL_OVER_OFI.value() && !GCFG_HCCL_GAUDI_DIRECT.value() && GCFG_HCL_RS_SO_RECV_CONT_REDUCTION.value()),
        "PDMA flow doesn't support RS_SO_RECV_CONT_REDUCTION");

    LOG_HCL_INFO(HCL, "scaleOutGNICs({})", scaleOutGNICs);

    // Check if GCFG_HCCL_GAUDI_DIRECT is set (by auto-detect / user)
    // if so, enable huge pages safety variable for RDMA: RDMAV_HUGEPAGES_SAFE
    // and disable sending inline data in Mellanox environment: MLX5_SCATTER_TO_CQE
    if (GCFG_HCCL_GAUDI_DIRECT.value())
    {
        // when using mpi, param configuration might not succeed- indicate the situation to the user
        int enableInline       = getEnvInt("MLX5_SCATTER_TO_CQE", -1);
        int rdmaVHugePagesSafe = getEnvInt("RDMAV_HUGEPAGES_SAFE", -1);
        if (enableInline != 0)
        {
            LOG_HCL_WARN(
                HCL,
                "MLX5_SCATTER_TO_CQE={}, to avoid sending inline data, attempting to set it to MLX5_SCATTER_TO_CQE=0",
                (enableInline == -1) ? "<not set>" : std::to_string(enableInline));
            setenv("MLX5_SCATTER_TO_CQE", "0", 0);
        }
        if (rdmaVHugePagesSafe != 1)
        {
            LOG_HCL_WARN(
                HCL,
                "RDMAV_HUGEPAGES_SAFE={}, to ensure huge pages safety, attempting to set it to RDMAV_HUGEPAGES_SAFE=1",
                (rdmaVHugePagesSafe == -1) ? "<not set>" : std::to_string(rdmaVHugePagesSafe));
            setenv("RDMAV_HUGEPAGES_SAFE", "1", 0);
        }
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