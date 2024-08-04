#include "hcl_config.h"

#include <arpa/inet.h>                        // for inet_ntoa, inet_ntop, inet_pton
#include <bits/exception.h>                   // for exception
#include <errno.h>                            // for errno
#include <ext/alloc_traits.h>                 // for __alloc_traits<>::value_type
#include <ifaddrs.h>                          // for ifaddrs, freeifaddrs, getifa...
#include <limits.h>                           // for USHRT_MAX
#include <netinet/in.h>                       // for sockaddr_in6, sockaddr_in
#include <cstring>                            // for strerror, memset, strcpy
#include <sys/socket.h>                       // for bind, listen, setsockopt
#include <unistd.h>                           // for close, gethostname
#include <algorithm>                          // for find, count
#include <cstdint>                            // for uint8_t, int32_t, uint32_t
#include <fstream>                            // for ifstream, operator<<, basic_...
#include <iterator>                           // for distance
#include <memory>                             // for allocator_traits<>::value_type
#include <nlohmann/json.hpp>                  // for json, basic_json, iter_impl

#include "hcl_global_conf.h"                  // for GCFG_*
#include "hccl_types.h"                       // for hcclResult_t, hcclSuccess, HCL_...
#include "hcl_utils.h"                        // for LOG_HCL_INFO, LOG_HCL_CRITICAL
#include "hlthunk.h"                          // for hlthunk_get_hw_ip_info, hlth...
#include "drm/habanalabs_accel.h"             // for hl_server_type, HL_SERVER_GA...
#include "hcl_log_manager.h"                  // for LOG_INFO, LOG_ERR, LOG_CRITICAL
#include "synapse_api_types.h"                // for synDeviceId
#include "platform/gen2_arch_common/types.h"  // for MAX_NICS_GEN2ARCH
#include "synapse_api.h"                      // for synDeviceGetInfoV2
#include "synapse_common_types.h"             // for synStatus

using json = nlohmannV340::json;

static inline std::string getAlignedString(std::string s, int alignment)
{
    int numSpaces = alignment - s.size();
    if (numSpaces <= 0)
    {
        return s;
    }

    std::string spaces(numSpaces, ' ');
    return s + spaces;
}

void hclGlobalConfigLog()
{
    LOG_INFO(HCL,
             "------------------- HCL Global configuration values -------------------\n"
             "Use CPU affinity:                      [{}]: {}\n"
             "Weak order:                            [{}]: {}\n"
             "QP congestion window:                  [{}]: {}\n"
             "QP congestion control enable:          [{}]: {}\n"
             "Scale out ports:                       [{}]: {}\n",
             getAlignedString(GCFG_USE_CPU_AFFINITY.primaryName(), 32),
             GCFG_USE_CPU_AFFINITY.getValueStr(),
             getAlignedString(GCFG_WEAK_ORDER.primaryName(), 32),
             GCFG_WEAK_ORDER.getValueStr(),
             getAlignedString(GCFG_CONGESTION_WINDOW.primaryName(), 32),
             GCFG_CONGESTION_WINDOW.getValueStr(),
             getAlignedString(GCFG_CONGESTION_CONTROL_ENABLE.primaryName(), 32),
             GCFG_CONGESTION_CONTROL_ENABLE.getValueStr(),
             getAlignedString(GCFG_SCALE_OUT_PORTS_MASK.primaryName(), 32),
             GCFG_SCALE_OUT_PORTS_MASK.getValueStr());
}

bool HclDeviceConfig::determineHclType()
{
    struct hlthunk_hw_ip_info hw_ip;
    hlthunk_get_hw_ip_info(m_fd, &hw_ip);
    const hl_server_type server_type = (hl_server_type)hw_ip.server_type;

    LOG_HCL_INFO(HCL, "Received server type from driver: {} ({})", server_type, (int)server_type);

    if (GCFG_BOX_TYPE.isSetFromUserConfig())
    {
        LOG_HCL_INFO(HCL, "Server type is set by user to {}, ignoring driver type", GCFG_BOX_TYPE.value());

        return validateHclType();
    }

    HclConfigType configTypeFromServer;
    switch (server_type)
    {
        case HL_SERVER_TYPE_UNKNOWN:
            configTypeFromServer = BACK_2_BACK;
            break;
        case HL_SERVER_GAUDI_HLS1:
            configTypeFromServer = HLS1;
            break;
        case HL_SERVER_GAUDI_HLS1H:
            configTypeFromServer = HLS1H;
            break;
        case HL_SERVER_GAUDI_TYPE1:
        case HL_SERVER_GAUDI_TYPE2:
            configTypeFromServer = OCP1;
            break;
        case HL_SERVER_GAUDI2_TYPE1:  // FALLTHROUGH
        case HL_SERVER_GAUDI2_HLS2:
            configTypeFromServer = HLS2;
            break;
        case HL_SERVER_GAUDI3_HLS3_FULL_OAM_3PORTS_SCALE_OUT:
            configTypeFromServer = HLS3;
            break;
        case HL_SERVER_GAUDI3_HL338:
            configTypeFromServer = HL338;
            break;
        default:
            LOG_HCL_CRITICAL(HCL, "Got unknown server_type ({}) from driver", hw_ip.server_type);
            configTypeFromServer = UNKNOWN;
            break;
    }

    GCFG_BOX_TYPE.setValue(m_boxTypeIdToStr[configTypeFromServer]);
    GCFG_BOX_TYPE_ID.setValue(configTypeFromServer);

    return validateHclType();
}

HclDeviceConfig::HclDeviceConfig(const synDeviceId deviceId) : m_deviceId(deviceId)
{
    if (deviceId != NO_DEVICE_ID)
    {
        synDeviceInfoV2 deviceInfo = {};

        VERIFY(synSuccess == synDeviceGetInfoV2(deviceId, &deviceInfo));
        m_fd              = deviceInfo.fd;
        m_deviceType      = deviceInfo.deviceType;
        std::string accel = getHLDevice(m_fd);
        uint32_t    oam   = getHwModuleId();
        LOG_HCL_INFO(HCL, "this rank is using device: {} OAM: {}", accel, oam);
    }

    m_nics.clear();
    m_disabledPorts = 0;
}

HclConfig::HclConfig(HclDeviceConfig& deviceConfig) : m_deviceConfig(deviceConfig) {}


bool HclDeviceConfig::parseDeviceConfig(const std::string& path)
{
    try
    {
        return _parseDeviceConfig(path);
    }
    catch (const std::exception& e)
    {
        LOG_HCL_ERR(HCL, " err: {}", e.what());

        return false;
    }

    return true;
}

bool HclDeviceConfig::validateHclType()
{
    if (m_fd == -1) return true; /* No device tests */

    /* No default in switch case to enforce adding new enums */
    HclConfigType configType = (HclConfigType)GCFG_BOX_TYPE_ID.value();

    switch (configType)
    {
        case HLS1:
        case HLS1H:
        case OCP1:
        case UNKNOWN:
            LOG_HCL_CRITICAL(HCL, "Invalid HCL_TYPE value ({})", configType);
            return false;
        case HLS2:
            if (!IS_DEVICE_GAUDI2(m_deviceType))
            {
                LOG_HCL_CRITICAL(HCL, "Invalid HCL_TYPE value ({}) for Gaudi2", configType);
                return false;
            }
            break;
        case HLS3:
        case HL338:
            if (!IS_DEVICE_GAUDI3(m_deviceType))
            {
                LOG_HCL_CRITICAL(HCL, "Invalid HCL_TYPE value ({}) for Gaudi3", configType);
                return false;
            }
            break;
        case BACK_2_BACK:
        case RING:
        case LOOPBACK:
            break;
    }

    return true;
}

bool HclDeviceConfig::parseGaudinet()
{
    json               gaudinetConfig;
    const char*        gaudinetFileCStr = GCFG_HCL_GAUDINET_CONFIG_FILE.value().c_str();
    std::ifstream      gaudinetFile(gaudinetFileCStr);
    std::string        old_gaudinet_file("/etc/gaudinet.json");

    // if file not found, check old default path (will be deprecated some time)
    if (!gaudinetFile.good())
    {
        gaudinetFileCStr = old_gaudinet_file.c_str();
        gaudinetFile.open(gaudinetFileCStr);
    }
    if (gaudinetFile.good())
    {
        LOG_HCL_INFO(HCL, "Loading Gaudi Net config at {}", gaudinetFileCStr);
        try
        {
            gaudinetFile >> gaudinetConfig;
            if (gaudinetConfig.find("NIC_NET_CONFIG") == gaudinetConfig.end())
            {
                LOG_HCL_ERR(HCL, "Invalid Gaudi Net Config File: NIC_NET_CONFIG key not found at {}", gaudinetFileCStr);
                return false;
            }
        }
        catch (const std::exception& e)
        {
            LOG_HCL_ERR(HCL, "Invalid json file {}, error {}", gaudinetFileCStr, e.what());
            return false;
        }
        auto nicConfigs = gaudinetConfig["NIC_NET_CONFIG"].get<std::vector<json>>();
        for (auto& nicConfig : nicConfigs)
        {
            if (nicConfig.find("NIC_MAC") == nicConfig.end())
            {
                LOG_HCL_ERR(HCL, "Invalid Gaudi Net Config File: NIC_MAC key not found at {}", gaudinetFileCStr);
                return false;
            }
            std::string nicMacStr = nicConfig["NIC_MAC"].get<std::string>();

            if (nicConfig.find("NIC_IP") == nicConfig.end())
            {
                LOG_HCL_ERR(HCL, "Invalid Gaudi Net Config File: NIC_IP key not found at {}", gaudinetFileCStr);
                return false;
            }
            std::string nicIpStr = nicConfig["NIC_IP"].get<std::string>();

            if (nicConfig.find("SUBNET_MASK") == nicConfig.end())
            {
                LOG_HCL_ERR(HCL, "Invalid Gaudi Net Config File: SUBNET_MASK key not found at {}", gaudinetFileCStr);
                return false;
            }
            std::string subnetMaskStr = nicConfig["SUBNET_MASK"].get<std::string>();

            if (nicConfig.find("GATEWAY_MAC") == nicConfig.end())
            {
                LOG_HCL_ERR(HCL, "Invalid Gaudi Net Config File: GATEWAY_MAC key not found at {}", gaudinetFileCStr);
                return false;
            }
            std::string gatewayMacStr = nicConfig["GATEWAY_MAC"].get<std::string>();

            uint32_t ip         = parseIpv4(nicIpStr);
            uint32_t subnetMask = parseIpv4(subnetMaskStr);
            if ((ip == 0) || (subnetMask == 0))
            {
                LOG_HCL_ERR(HCL, "Invalid ipv4 address: IP Address ({}), SubnetMask ({})", nicIpStr, subnetMaskStr);
                return false;
            }
            auto          gatewayMac = parseMac(gatewayMacStr);
            auto          nicMac     = parseMac(nicMacStr);
            HclNicNetInfo netInfo {ip, subnetMask, gatewayMac};

            LOG_HCL_DEBUG(
                HCL,
                "Gaudi Net Config: NIC MAC Address '{}'(0x{:x}) => IP Address '{}', Subnet MASK '{}', GW MAC Address '{}'(0x{:x})",
                nicMacStr,
                nicMac,
                ip2str(ip),
                ip2str(subnetMask),
                gatewayMacStr,
                gatewayMac);
            m_gaudiNet.insert({nicMac, netInfo});
        }
    }
    else
    {
        LOG_HCL_INFO(HCL, "No L3 Gaudi Net config file was found at {}. Assuming L2 configuration", gaudinetFileCStr);
    }

    return true;
}

bool HclDeviceConfig::parseDeviceJsonConfig(json& config)
{
    if (config.find("HCL_TYPE") != config.end())
    {
        GCFG_BOX_TYPE.setFromString(config["HCL_TYPE"].get<std::string>());
        LOG_HCL_INFO(HCL, "HCL_TYPE from json: {}", GCFG_BOX_TYPE.value());
        if (!validateHclType())
        {
            return false;
        }
    }
    else
    {
        if (!determineHclType())
        {
            return false;
        }

        LOG_HCL_INFO(HCL, "HCL_TYPE from driver: {}", GCFG_BOX_TYPE.value());
    }

    if (getHostName().empty())
    {
        LOG_HCL_ERR(HCL, "Failed to init hostname");
        return false;
    }

    if (config.find("DISABLED_PORTS") != config.end())
    {
        m_disabledPorts = config["DISABLED_PORTS"].get<std::set<unsigned>>();

        if (GCFG_BOX_TYPE.value() == "HLS2" || GCFG_BOX_TYPE.value() == "HLS3" || GCFG_BOX_TYPE.value() == "HL338")
        {
            struct hlthunk_nic_get_ports_masks_out ports_masks;
            uint64_t                               disabled_nics_mask;
            int                                    ret = hlthunk_nic_get_ports_masks(m_fd, &ports_masks);
            if (ret)
            {
                LOG_ERR(HCL, "Could not read port mask from hl-thunk: {}", ret);
                disabled_nics_mask = -1;
            }
            else
            {
                disabled_nics_mask = ~ports_masks.ports_mask;
            }
            VERIFY(disabled_nics_mask != (unsigned)-1, "Ports mask was not defined.");
            updateDisabledPorts(disabled_nics_mask);
        }
    }

    if (config.find("SKIP_SYNC") != config.end())
    {
        m_skipSync = config["SKIP_SYNC"].get<bool>();
    }

    if (config.find("HCL_NICS") != config.end())
    {
        /**
     * Parse the HCL_NICS section in input json config file
     * This table will define for each card location in the HLS the nics wiring.
     * 1000: means this nic going to the switch and can talk with any other rank.
     * 100:  means this nic going to the switch and can talk with only peer ranks.
     * -1:   means this nic not connected.
         */
        std::vector<json> cards = config["HCL_NICS"].get<std::vector<json>>();
        VERIFY(cards.size() == DEFAULT_BOX_SIZE);

        parseNicsHLS2(cards);
    }

    return true;
}

bool HclDeviceConfig::_parseDeviceConfig(std::string path)
{
    // Parse Gaudi Net first
    if (!parseGaudinet())
    {
        LOG_HCL_ERR(HCL, "Parsing Gaudi net file failed");
        return false;
    }

    json config;
    LOG_HCL_INFO(HCL, "Calling parseDeviceJsonConfig");

    return parseDeviceJsonConfig(config);
}

void HclDeviceConfig::determineDisabledNicsForLoopbackTests()
{
    std::string disabledNicsAsString(GCFG_LOOPBACK_DISABLED_NICS.value());

    if (disabledNicsAsString.empty() == true)
    {
        return;
    }
    LOG_HCL_DEBUG(HCL, "disabledNicsAsString={}", disabledNicsAsString);

    uint64_t currentIntegerStartingIndex = 0;

    for (uint64_t index = 0; index < disabledNicsAsString.size() + 1; ++index)
    {
        if (index == disabledNicsAsString.size() || disabledNicsAsString[index] == ',')
        {
            std::string currentNicIdToDisableAsString(disabledNicsAsString,
                                                      currentIntegerStartingIndex,
                                                      index - currentIntegerStartingIndex);

            m_disabledPorts.set(std::stoi(currentNicIdToDisableAsString));

            currentIntegerStartingIndex = index + 1;
        }
    }

    LOG_HCL_TRACE(HCL, "disabled ports: {}", m_disabledPorts.to_str());
}

void HclDeviceConfig::parseNicsHLS2(const std::vector<json>& cards)
{
    for (auto card : cards)
    {
        int  deviceId = card["CARD_LOCATION"].get<int>();
        auto nics     = card["NICS"].get<std::vector<json>>();
        for (auto& remoteDescriptor : nics)
        {
            m_nics[deviceId].emplace_back(remoteDescriptor["REMOTE_CARD"], remoteDescriptor["REMOTE_NIC"]);
        }
    }
}

uint32_t HclDeviceConfig::getHwModuleId()
{
    if (m_hwModuleID == (unsigned)-1)
    {
        struct hlthunk_hw_ip_info hw_ip;
        hlthunk_get_hw_ip_info(m_fd, &hw_ip);

        // Align Device ID (Rank) to box size to enable physical box partition
        m_hwModuleID = hw_ip.module_id;
    }
    return m_hwModuleID;
}

std::string HclDeviceConfig::getHostName()
{
    if (m_hostnameLength == 0)
    {
        m_hostnameLength = gethostname(m_hostname, HOSTNAME_MAX_LENGTH);
        if (m_hostnameLength == -1)
        {
            LOG_ERR(HCL, "gethostname failed with error ({})", strerror(errno));
            memset(m_hostname, 0, HOSTNAME_MAX_LENGTH);
        }
        else if (m_hostnameLength >= HOSTNAME_MAX_LENGTH)
        {
            LOG_ERR(HCL, "hostname size is bigger than HOSTNAME_MAX_LENGTH ({})", HOSTNAME_MAX_LENGTH);
            memset(m_hostname, 0, HOSTNAME_MAX_LENGTH);
        }
    }
    return m_hostname;
}

void HclDeviceConfig::fillDeviceInfo(RankInfoHeader& dest)
{
    dest.hwModuleID      = getHwModuleId();
    if (!isLoopbackMode())
    {
        std::string hostname = getHostName();
        strcpy(dest.hostname, hostname.c_str());
        dest.hostnameLength = hostname.size();
    }
}

uint64_t HclDeviceConfig::getHclReservedSramSize()
{
    if (m_hclReservedSramSize == 0)
    {
        m_hclReservedSramSize = GCFG_HCL_SRAM_SIZE_RESERVED_FOR_HCL.value();
    }
    return m_hclReservedSramSize;
}

uint64_t HclDeviceConfig::getSramBaseAddress()
{
    if (m_sramBaseAddress == 0)
    {
        hlthunk_hw_ip_info hw_info;
        hlthunk_get_hw_ip_info(m_fd, &hw_info);
        m_sramBaseAddress = hw_info.sram_base_address;
    }
    return m_sramBaseAddress;
}

void HclDeviceConfig::updateDisabledPorts(const uint64_t disabledPortsMaskFromLkd,
                                          const uint64_t forcedLoopBackScaleoutDisabledPortsMask)
{
    LOG_HCL_DEBUG(HCL,
                  "disabledPortsMaskFromLkd={:024b}, forcedLoopBackScaleoutDisabledPortsMask={:024b}",
                  disabledPortsMaskFromLkd,
                  forcedLoopBackScaleoutDisabledPortsMask);

    uint64_t activeMask = disabledPortsMaskFromLkd;
    if (isLoopbackMode() &&
        (forcedLoopBackScaleoutDisabledPortsMask != 0))  // For G3 loopback, its different scaleout port mask per device
    {
        m_disabledPorts = 0;
        activeMask = disabledPortsMaskFromLkd | forcedLoopBackScaleoutDisabledPortsMask;
    }

    m_disabledPorts |= activeMask;
}

bool HclDeviceConfig::init()
{
    if (!parseDeviceConfig(GCFG_HCL_DEVICE_CONFIG.value()))
    {
        LOG_ERR(HCL, "{}: parseDeviceConfig failed", __FUNCTION__);
        return false;
    }

    if (isLoopbackMode())
    {
        // For loopback tests, determine the disabled NIC's. At any scenario the
        // scale out ports must always be disabled. For Gaudi2 they are 8,22,23
        determineDisabledNicsForLoopbackTests();
    }

    bool res = getHclReservedSramSize();
    res &= getSramBaseAddress();
    getHwModuleId();
    return res;
}

bool HclConfig::init(HCL_Rank rank, uint32_t ranksCount)
{
    VERIFY(m_commSize == 0 && m_jsonIndex == -1, "rank and count were already set");

    m_commSize  = ranksCount;
    m_jsonIndex = rank;

    if (isLoopbackMode())
    {
        // For loopback tests, determine the communicator size. As there is only
        // one process that actually is running, but in order to test collective
        // routines, more than one rank needs to exist in the communicator
        m_commSize = GCFG_LOOPBACK_COMMUNICATOR_SIZE.value();
    }

    return true;
}
