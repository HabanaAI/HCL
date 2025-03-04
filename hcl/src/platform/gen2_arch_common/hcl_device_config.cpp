#include "platform/gen2_arch_common/hcl_device_config.h"

#include <bits/exception.h>   // for exception
#include <errno.h>            // for errno
#include <cstring>            // for strerror, memset, strcpy
#include <unistd.h>           // for gethostname
#include <algorithm>          // for find, count
#include <cstdint>            // for uint*_t
#include <fstream>            // for ifstream, operator<<, basic_...
#include <nlohmann/json.hpp>  // for json, basic_json, iter_impl

#include "hcl_types.h"        // for SYN_VALID_DEVICE_ID
#include "hcl_global_conf.h"  // for GCFG_*
#include "hcl_utils.h"        // for LOG_*
#include "network_utils.h"
#include "hccl_device.h"

using json = nlohmannV340::json;

const std::map<HclConfigType, std::string> g_boxTypeIdToStr = {{BACK_2_BACK, "BACK_2_BACK"},
                                                               {LOOPBACK, "LOOPBACK"},
                                                               {RING, "RING"},
                                                               {HLS1, "HLS1"},
                                                               {OCP1, "OCP1"},
                                                               {HLS1H, "HLS1-H"},
                                                               {HLS2, "HLS2"},
                                                               {HL288, "HL288"},
                                                               {HLS3, "HLS3"},
                                                               {HL338, "HL338"},
                                                               {UNKNOWN, "UNKNOWN"},
};

constexpr char BOOT_ID_FILE[] = "/proc/sys/kernel/random/boot_id";

HclDeviceConfig::HclDeviceConfig()
{
    m_hclReservedSramSize = GCFG_HCL_SRAM_SIZE_RESERVED_FOR_HCL.value();

    initHostName();

    m_nics.clear();
}

bool HclDeviceConfig::parseDeviceConfig()
{
    try
    {
        if (GCFG_HCL_USE_NET_DETECT.value())
        {
            net_itfs_map_t nis = get_net_itfs();

            LOG_HCL_INFO(HCL, "detected network interfaces: ");

            for (const auto& [iname, net_itf] : nis)
            {
                LOG_HCL_INFO(HCL,
                             "    {}{}: mac: 0x{:x} ,ip4: {} (0x{:x}), ip6: {} (0x{:x})",
                             iname,
                             net_itf.gaudi ? "(gaudi)" : "",
                             net_itf.mac,
                             ip2str(net_itf.ip4),
                             net_itf.ip4,
                             ip2str(net_itf.ip6),
                             net_itf.ip6);

                HclNicNetInfo netInfo {net_itf.ip4, 0, 0};
                m_gaudiNet.insert({net_itf.mac, netInfo});
            }
        }
        else if (!parseGaudinet())
        {
            LOG_HCL_ERR(HCL, "Parsing Gaudi net file failed");
            return false;
        }
    }
    catch (const std::exception& e)
    {
        LOG_HCL_ERR(HCL, " err: {}", e.what());
        return false;
    }

    if (!determineHclType())
    {
        return false;
    }
    LOG_HCL_INFO(HCL, "HCL_TYPE from driver: {}", GCFG_BOX_TYPE.value());

    return true;
}

bool HclDeviceConfig::parseGaudinet()
{
    json          gaudinetConfig;
    const char*   gaudinetFileCStr = GCFG_HCL_GAUDINET_CONFIG_FILE.value().c_str();
    std::ifstream gaudinetFile(gaudinetFileCStr);
    std::string   old_gaudinet_file("/etc/gaudinet.json");

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

            LOG_HCL_DEBUG(HCL,
                          "Gaudi Net Config: NIC MAC Address '{}'(0x{:x}) => IP Address '{}', Subnet MASK '{}', GW MAC "
                          "Address '{}'(0x{:x})",
                          nicMacStr,
                          nicMac,
                          ip2str(ip),
                          ip2str(subnetMask),
                          gatewayMacStr,
                          gatewayMac);
            m_gaudiNet.insert({nicMac, netInfo});
        }
        LOG_INFO_F(HCL, "L3 Gaudi Net config file was found at {}. Using L3 configuration", gaudinetFileCStr);

        m_L3 = true;
    }
    else
    {
        LOG_INFO_F(HCL, "No L3 Gaudi Net config file was found at {}. Assuming L2 configuration", gaudinetFileCStr);
    }

    return true;
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

void HclDeviceConfig::initHostName()
{
    // Read and validate hostname
    const int rc = gethostname(m_hostname, HOSTNAME_MAX_LENGTH);
    if (rc != 0)
    {
        LOG_HCL_ERR(HCL, "gethostname failed with error ({})", strerror(errno));
        memset(m_hostname, 0, HOSTNAME_MAX_LENGTH);
    }
    else if (m_hostname[HOSTNAME_MAX_LENGTH - 1] != 0)  // if string is not null terminated, we have overflow
    {
        LOG_HCL_ERR(HCL, "hostname size is bigger than HOSTNAME_MAX_LENGTH ({})", HOSTNAME_MAX_LENGTH);
        memset(m_hostname, 0, HOSTNAME_MAX_LENGTH);
    }
    else
    {
        // Read boot_id
        std::ifstream boot_id_file(BOOT_ID_FILE);
        if (!boot_id_file)
        {
            LOG_HCL_DEBUG(HCL, "Failed to open boot_id file, using hostname without boot_id");
        }
        else
        {
            std::string boot_id;
            std::getline(boot_id_file, boot_id);
            boot_id_file.close();

            // Concat boot_id to hostname
            if (strlen(m_hostname) + boot_id.length() < (int)sizeof(m_hostname))
            {
                std::strcat(m_hostname, boot_id.c_str());
            }
            else
            {
                LOG_HCL_DEBUG(
                    HCL,
                    "hostname and boot_id size is bigger than HOSTNAME_MAX_LENGTH ({}), using hostname without boot_id",
                    HOSTNAME_MAX_LENGTH);
            }
        }
    }

    LOG_HCL_DEBUG(HCL, "Setting m_hostname={}", std::string(m_hostname));
}

void HclDeviceConfig::fillDeviceInfo(RankInfoHeader& dest)
{
    dest.hwModuleID = getHwModuleId();
    dest.L3         = m_L3;

    if (!isLoopbackMode())
    {
        std::string hostname = getHostName();
        strcpy(dest.hostname, hostname.c_str());
        dest.hostnameLength          = hostname.size();
        dest.failedScaleOutPortsMask = hccl_device()->getFailedScaleOutPortsMask();
        LOG_HCL_DEBUG(HCL, "m_failedScaleOutPortsMask={:024b}", (uint64_t)hccl_device()->getFailedScaleOutPortsMask());
    }
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
        activeMask      = disabledPortsMaskFromLkd | forcedLoopBackScaleoutDisabledPortsMask;
    }

    m_disabledPorts |= activeMask;
}

bool HclDeviceConfig::init()
{
    if (!parseDeviceConfig())
    {
        LOG_HCL_ERR(HCL, "parseDeviceConfig failed");
        return false;
    }

    if (isLoopbackMode())
    {
        // For loopback tests, determine the disabled NIC's. At any scenario the
        // scale out ports must always be disabled. For Gaudi2 they are 8,22,23
        determineDisabledNicsForLoopbackTests();
    }

    return true;
}