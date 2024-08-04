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
#include "hcl_types.h"                                      // for HCL_HwModuleId

typedef std::array<uint32_t, GEN2ARCH_HLS_BOX_SIZE> RemoteDevicePortMasksArray;  // 24 bits per device

typedef std::array<uint16_t, GEN2ARCH_HLS_BOX_SIZE>
    DeviceNicsMacrosMask;  // per device module id, a dup mask with bit set for nic macro it belongs to. (Only scaleup
                           // nic macros appear here)

typedef uint16_t                                        NicMacroIndexType;
typedef std::vector<NicMacroIndexType>                  NicMacrosPerDevice;  // vector of nic macro indexes
typedef std::array<NicMacrosPerDevice, GEN2ARCH_HLS_BOX_SIZE>
    NicMacrosDevicesArray;  // an array of vector of macros indexes for all devices. Only scaleup related nic macros
                            // appear here

typedef std::unordered_set<HCL_HwModuleId>
    DevicesSet;  // a set of module id numbers that belong one of the nic macros sets

extern const ServerNicsConnectivityArray g_HLS3NicsConnectivityArray;
extern const ServerNicsConnectivityArray g_HLS3PcieNicsConnectivityArray;

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

    virtual void   onCommInit(HclDynamicCommunicator& dynamicComm) override;
    const uint32_t getDeviceToRemoteIndexPortMask(HclDynamicCommunicator& dynamicComm,
                                                  box_devices_t&          deviceToRemoteIndex);
    const uint32_t getRemoteDevicePortMask(uint32_t moduleId, HclDynamicCommunicator& dynamicComm);
    const uint32_t getInnerRanksPortMask(HclDynamicCommunicator& dynamicComm);
    const uint32_t                    getRankToPortMask(const HCL_Rank rank, HclDynamicCommunicator& dynamicComm);
    unsigned       getDefaultScaleOutPortByIndex(unsigned idx) const override;
    virtual void                      assignCustomMapping(const Gen2ArchPortMappingConfig& portMappingConfig) override;
    const RemoteDevicePortMasksArray& getRemoteDevicesPortMasks() const { return m_remoteDevicePortMasks; }
    uint16_t getNicsMacrosDupMask(const uint32_t remoteDevice) const { return m_nicsMacrosDupMask[remoteDevice]; }
    const NicMacrosPerDevice& getNicMacrosPerDevice(const uint32_t remoteDevice) const
    {
        return m_nicMacrosDevices[remoteDevice];
    }
    const DevicesSet& getDevicesSet(const bool first) const
    {
        return (first ? m_macroDevicesSet0 : m_macroDevicesSet1);
    }
    nics_mask_t getRemoteScaleOutPorts(const uint32_t remoteModuleId,
                                    const unsigned spotlightType = DEFAULT_SPOTLIGHT);  // Get a remote device scaleout ports
    unsigned getRemoteSubPortIndex(const uint32_t remoteModuleId,
                                   const uint8_t  remotePort,
                                   const unsigned spotlightType = DEFAULT_SPOTLIGHT)
        const;  // Get a remote device sub nic index for the remote port

    const hcl::Gaudi3Hal& getHal() const { return m_hal; }
    const NicMacroIndexType getScaleupNicsMacrosCount() const { return m_scaleupNicsMacrosCount; }

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

    // A nic macro pair struct describes which devices this nic Macro is connected to and if its
    // scaleup/scaleout/mixed/not connected macro
    struct NicMacroPair
    {
        uint32_t               m_device0    = 0;  // always have value
        uint32_t               m_device1    = 0;  // may have value if shared
        NicMacroPairNicsConfig m_nicsConfig = NIC_MACRO_NO_SCALEUP_NICS;
    };

    typedef std::array<struct NicMacroPair, NIC_MAX_NUM_OF_MACROS>
        NicMacroPairs;  // All the nic macros pairs of specific device

    void         init(const ServerNicsConnectivityArray& serverNicsConnectivityArray,
                      const Gen2ArchPortMappingConfig&   portMappingConfig,
                      const bool                         setPortsMask = true);
    virtual void assignDefaultMapping() override;  // not used for G3
    void         assignDefaultMapping(const ServerNicsConnectivityArray& serverNicsConnectivityArray);
    void         initNicMacros();
    void         initDeviceSetsAndDupMasks();
    void         initNicMacrosForAllDevices();
    bool isRemoteScaleoutPort(const uint32_t remoteModuleId,
                              const uint8_t  remotePort,
                              const unsigned spotlightType = DEFAULT_SPOTLIGHT) const;

    std::vector<HCL_Comm>      m_innerRanksPortMask    = {};
    RemoteDevicePortMasksArray m_remoteDevicePortMasks = {};
    NicMacroPairs              m_nicMacroPairs         = {};  // All the nic macros pairs of our device
    DevicesSet                 m_macroDevicesSet0;            // first set of module Ids that can be aggregated together
    DevicesSet                 m_macroDevicesSet1;  // second set of module Ids that can be aggregated together
    DeviceNicsMacrosMask       m_nicsMacrosDupMask = {};
    NicMacrosDevicesArray      m_nicMacrosDevices  = {};
    NicMacroIndexType          m_scaleupNicsMacrosCount = 0;  // number of scaleup nic macros using dup mask
};
