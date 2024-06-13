#pragma once

#include <cstdint>                                  // for uint8_t
#include <array>                                    // for array
#include <map>                                      // for map
#include <utility>                                  // for pair
#include <vector>                                   // for vector
#include <unordered_set>                            // for unordered_set

#include "platform/gen2_arch_common/port_mapping.h"  // for Gen2ArchDevicePortMapping, ServerNicsConnectivityArray
#include "platform/gen2_arch_common/types.h"         // for MAX_NICS_GEN2ARCH, GEN2ARCH_HLS_BOX_SIZE
#include "hcl_dynamic_communicator.h"                // for HclDynamicCommunicator
#include "platform/gen2_arch_common/port_mapping_config.h"  // for Gen2ArchPortMappingConfig
#include "gaudi3/gaudi3.h"                                  // for NIC_MAX_NUM_OF_MACROS
#include "platform/gaudi3/hal.h"                            // for Gaudi3Hal

typedef std::array<uint32_t, GEN2ARCH_HLS_BOX_SIZE> RemoteDevicePortMasksArray;  // 24 bits per device

typedef std::array<uint16_t, GEN2ARCH_HLS_BOX_SIZE>
    DeviceNicsMacrosMask;  // per device module id, a dup mask with bit set for nic macro it belongs to (2 bits per
                           // device). Only scaleup nic macros appear here (11 out of 12)

typedef uint16_t                                        NicMacroIndexType;
typedef std::pair<NicMacroIndexType, NicMacroIndexType> MacroPairDevice;  // pair of nic macro indexes
typedef std::array<MacroPairDevice, GEN2ARCH_HLS_BOX_SIZE>
    MacroPairDevicesArray;  // an array of macro pairs indexes for all devices. Only scaleup macro pair appear here

typedef std::unordered_set<uint32_t> DevicesSet;  // a set of module id numbers that belong one of the nic macro pairs

extern const ServerNicsConnectivityArray g_HLS3NicsConnectivityArray;

class Gaudi3DevicePortMapping : public Gen2ArchDevicePortMapping
{
public:
    Gaudi3DevicePortMapping(
        const int                          fd,
        const Gen2ArchPortMappingConfig&   portMappingConfig,
        const hcl::Gaudi3Hal&              hal,
        const ServerNicsConnectivityArray& serverNicsConnectivityArray = g_HLS3NicsConnectivityArray);
    Gaudi3DevicePortMapping(
        const int                          fd,
        const hcl::Gaudi3Hal&              hal,
        const ServerNicsConnectivityArray& serverNicsConnectivityArray = g_HLS3NicsConnectivityArray);  // for testing
    Gaudi3DevicePortMapping(
        const int                          fd,
        const int                          moduleId,
        const hcl::Gaudi3Hal&              hal,
        const ServerNicsConnectivityArray& serverNicsConnectivityArray = g_HLS3NicsConnectivityArray);  // for testing
    virtual ~Gaudi3DevicePortMapping() = default;

    const uint32_t getDeviceToRemoteIndexPortMask(HclDynamicCommunicator& dynamicComm,
                                                  box_devices_t&          deviceToRemoteIndex);
    const uint32_t getRemoteDevicePortMask(uint32_t moduleId, HclDynamicCommunicator& dynamicComm);
    const uint32_t getInnerRanksPortMask(HclDynamicCommunicator& dynamicComm);
    const uint32_t getRankToPortMask(const HCL_Rank rank, HclDynamicCommunicator& dynamicComm);
    unsigned       getMaxNumScaleOutPorts() const override;
    unsigned       getDefaultScaleOutPortByIndex(unsigned idx) const override;
    virtual void   assignCustomMapping(int fd, const Gen2ArchPortMappingConfig& portMappingConfig) override;
    const RemoteDevicePortMasksArray& getRemoteDevicesPortMasks() const { return m_remoteDevicePortMasks; }
    uint16_t getNicsMacrosDupMask(const uint32_t remoteDevice) const { return m_nicsMacrosDupMask[remoteDevice]; }
    const MacroPairDevice& getMacroPairDevices(const uint32_t remoteDevice) const
    {
        return m_macroPairDevices[remoteDevice];
    }
    const DevicesSet& getPairSet(const bool first) const { return (first ? m_macroPairsSet0 : m_macroPairsSet1); }
    nics_mask_t getRemoteScaleOutPorts(const uint32_t remoteModuleId,
                                    const unsigned spotlightType = DEFAULT_SPOTLIGHT);  // Get a remote device scaleout ports
    unsigned getRemoteSubPortIndex(const uint32_t remoteModuleId,
                                   const uint8_t  remotePort,
                                   const unsigned spotlightType = DEFAULT_SPOTLIGHT)
        const;  // Get a remote device sub nic index for the remote port

    const hcl::Gaudi3Hal& getHal() const { return m_hal; }

protected:
    const hcl::Gaudi3Hal& m_hal;

private:
    typedef enum
    {
        NIC_MACRO_NO_SCALEUP_NICS = 0,
        NIC_MACRO_NOT_CONNECTED_NICS,
        NIC_MACRO_SINGLE_SCALEUP_NIC,
        NIC_MACRO_TWO_SCALEUP_NICS
    } NicMacroPairNicsConfig;
    struct NicMacroPair
    {
        uint32_t               m_device0    = 0;  // always have value
        uint32_t               m_device1    = 0;  // may have value if shared
        NicMacroPairNicsConfig m_nicsConfig = NIC_MACRO_NO_SCALEUP_NICS;
    };

    typedef std::array<struct NicMacroPair, NIC_MAX_NUM_OF_MACROS> NicMacroPairs;

    void         init(const ServerNicsConnectivityArray& serverNicsConnectivityArray);
    virtual void assignDefaultMapping() override;  // not used for G3
    void         assignDefaultMapping(const ServerNicsConnectivityArray& serverNicsConnectivityArray);
    void initNinMacroPairs();
    void initPairsSetsAndDupMasks();
    void initMacroPairDevices();
    bool isRemoteScaleoutPort(const uint32_t remoteModuleId,
                              const uint8_t  remotePort,
                              const unsigned spotlightType = DEFAULT_SPOTLIGHT) const;

    std::vector<HCL_Comm>      m_innerRanksPortMask    = {};
    RemoteDevicePortMasksArray m_remoteDevicePortMasks = {};
    NicMacroPairs              m_nicMacroPairs         = {};
    DevicesSet                 m_macroPairsSet0;  // first set of module Ids that can be aggregated together
    DevicesSet                 m_macroPairsSet1;  // second set of module Ids that can be aggregated together
    DeviceNicsMacrosMask       m_nicsMacrosDupMask = {};
    MacroPairDevicesArray      m_macroPairDevices  = {};
};
