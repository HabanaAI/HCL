#pragma once

#include <cstdint>        // for uint8_t, uint32_t
#include <map>            // for map
#include <string>         // for string
#include <unordered_map>  // for unordered_map
#include <utility>        // for pair
#include <vector>         // for vector

#include "hcl_types.h"  // for RankInfoHeader, HOSTNAME_MAX_LENGTH, SYN_VALID_DEVICE_ID
#include "hcl_bits.h"   // for nics_mask_t

constexpr int INVALID_HW_MODULE_ID = -1;
constexpr int PCI_ID_STR_LEN       = 13;
/**
 * Gaudi NIC subnet info
 */
struct HclNicNetInfo
{
    uint32_t ipAddress;         /* IP address of the port */
    uint32_t subnetMask;        /* Mask of the port subnet */
    uint64_t gatewayMacAddress; /* MAC address of the gateway to leave the subnet */
};

extern const std::map<HclConfigType, std::string> g_boxTypeIdToStr;

class HclDeviceConfig
{
public:
    HclDeviceConfig();
    HclDeviceConfig(const HclDeviceConfig&)            = delete;
    HclDeviceConfig& operator=(const HclDeviceConfig&) = delete;
    virtual ~HclDeviceConfig()                         = default;

    bool init();

    uint32_t          getHwModuleId() const { return m_hwModuleID; }
    int               getFd() const { return m_fd; }
    const std::string getHostName() { return m_hostname; }
    const int         getDeviceIndex() const { return m_deviceIndex; }
    const char*       getDevicePciBusId() const { return m_pciBusId; }

    void fillDeviceInfo(RankInfoHeader& dest);

    virtual const std::string                          getDeviceTypeStr() const = 0;
    uint64_t                                           getHclReservedSramSize() const { return m_hclReservedSramSize; }
    uint64_t                                           getSramBaseAddress() const { return m_sramBaseAddress; }
    bool                                               getDramEnabled() const { return m_dramEnabled; }
    nics_mask_t                                        getDisabledPorts() const { return m_disabledPorts; }
    void                                               updateDisabledPorts(const uint64_t disabledPortsMaskFromLkd,
                                                                           const uint64_t forcedLoopBackScaleoutDisabledPortsMask = 0);
    const std::unordered_map<uint64_t, HclNicNetInfo>& getGaudiNet() const { return m_gaudiNet; }
    virtual bool                                       isDeviceAcquired() const = 0;

protected:
    virtual void readHwType()       = 0;
    virtual bool determineHclType() = 0;
    virtual bool validateHclType()  = 0;
    bool         parseDeviceConfig();
    void         initHostName();  // called in init of runtime

    void determineDisabledNicsForLoopbackTests();
    bool parseGaudinet();

    int                                         m_fd                            = -1;
    uint64_t                                    m_hclReservedSramSize           = 0;
    uint64_t                                    m_sramBaseAddress               = 0;
    uint32_t                                    m_hwModuleID                    = INVALID_HW_MODULE_ID;
    nics_mask_t                                 m_disabledPorts                 = 0;
    char                                        m_hostname[HOSTNAME_MAX_LENGTH] = {0};
    bool                                        m_dramEnabled                   = true;
    std::unordered_map<uint64_t, HclNicNetInfo> m_gaudiNet;  // Mapping between NIC MAC address and NIC's subnet info
    int                                         m_deviceIndex;
    char                                        m_pciBusId[PCI_ID_STR_LEN];

    //       card_id:  [(dest_card, dest_nic), (dest_card, dest_nic), ...]
    std::map<unsigned, std::vector<std::pair<uint8_t, uint8_t>>> m_nics =
        std::map<unsigned, std::vector<std::pair<uint8_t, uint8_t>>>();
};
