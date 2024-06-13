#pragma once

#include <cstdint>                                // for uint8_t, uint32_t
#include <list>                                   // for list
#include <map>                                    // for map
#include <set>                                    // for set
#include <string>                                 // for string
#include <unordered_map>                          // for unordered_map
#include <utility>                                // for pair
#include <vector>                                 // for vector
#include <nlohmann/json.hpp>                      // for json

#include "hccl_types.h"                           // for hcclResult_t, HCL_INVA...
#include "hcl_types.h"                            // for HclConfigType, BACK...
#include "interfaces/hcl_unique_sorted_vector.h"  // for UniqueSortedVector
#include "synapse_api_types.h"                    // for synDeviceId
#include "synapse_common_types.h"                 // for synDeviceType, synD...

using json = nlohmannV340::json;

/**
 * Gaudi NIC subnet info
 */
struct HclNicNetInfo
{
    uint32_t ipAddress;         /* IP address of the port */
    uint32_t subnetMask;        /* Mask of the port subnet */
    uint64_t gatewayMacAddress; /* MAC address of the gateway to leave the subnet */
};

/**
* Print the values of the Hcl global config
*/
void hclGlobalConfigLog();

class HclDeviceConfig
{
public:
    HclDeviceConfig() = default;

    HclDeviceConfig(const synDeviceId deviceId);

    /**
     * _parseDeviceConfig wrapper, to also catch exceptions (thrown by nlohmann)
     * @return true  - on success parsing
     *         false - on any error (file not found, mandatory key missing)
     */
    bool parseDeviceConfig(const std::string& path);

    bool init();

    bool     determineHclType();
    bool     validateHclType();
    uint32_t getHwModuleId();
    bool     parseGaudinet();

    std::string getHostName();

    void fillDeviceInfo(RankInfoHeader& dest);

    uint64_t getHclReservedSramSize();
    uint64_t getSramBaseAddress();
    void     updateDisabledPorts(const uint64_t disabledPortsMaskFromLkd,
                                 const uint64_t forcedLoopBackScaleoutDisabledPortsMask = 0);

    int           m_fd         = -1;
    synDeviceId   m_deviceId   = NO_DEVICE_ID;
    synDeviceType m_deviceType = synDeviceTypeInvalid;

    uint32_t m_hwModuleID          = -1;
    uint64_t m_sramBaseAddress     = 0;
    uint64_t m_hclReservedSramSize = 0;

    std::unordered_map<uint64_t, HclNicNetInfo> m_gaudiNet;  // Mapping between NIC MAC address and NIC's subnet info

    nics_mask_t m_disabledPorts;

    //       card_id:  [(dest_card, dest_nic), (dest_card, dest_nic), ...]
    std::map<unsigned, std::vector<std::pair<uint8_t, uint8_t>>> m_nics =
        std::map<unsigned, std::vector<std::pair<uint8_t, uint8_t>>>();

    bool m_ocp1Mapping = false;
    bool m_skipSync    = false;

    char m_hostname[HOSTNAME_MAX_LENGTH] = {0};
    int  m_hostnameLength                = 0;

    std::map<HclConfigType, std::string> m_boxTypeIdToStr = {{BACK_2_BACK, "BACK_2_BACK"},
                                                             {LOOPBACK, "LOOPBACK"},
                                                             {RING, "RING"},
                                                             {HLS1, "HLS1"},
                                                             {OCP1, "OCP1"},
                                                             {HLS1H, "HLS1-H"},
                                                             {HLS2, "HLS2"},
                                                             {HLS3, "HLS3"},
                                                             {HLS3PCIE, "HLS3PCIE"},
                                                             {UNKNOWN, "UNKNOWN"}};

private:
    bool _parseDeviceConfig(std::string path);
    bool parseDeviceJsonConfig(json& config);

    void determineDisabledNicsForLoopbackTests();

    void parseNicsHLS2(const std::vector<json>& config);
};

/**
 * @class HclConfig is responsible to parse the HCL JSON configuration file passed to HCL_Init
 */
class HclConfig
{
public:
    HclConfig() = default;
    HclConfig(HclDeviceConfig& deviceConfig);

    bool init(HCL_Rank rank, uint32_t ranksCount);

    uint32_t                        m_commSize = 0;
    std::vector<UniqueSortedVector> m_communicators;  // list of communicators

    int                       m_jsonIndex         = -1;

    HclDeviceConfig m_deviceConfig;

private:

};
