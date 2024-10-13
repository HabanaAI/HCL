#pragma once

#include <cstdint>  // for uint*_t

#include "platform/gen2_arch_common/runtime_connectivity.h"       // for Gen2ArchRuntimeConnectivity
#include "platform/gen2_arch_common/server_connectivity_types.h"  // for
#include "platform/gaudi3/nic_macro_types.h"
#include "hcl_bits.h"  // for nics_mask_t

// forward decl
class Gaudi3BaseServerConnectivity;
class HclDynamicCommunicator;

//
// Configuration per comm
//
class Gaudi3BaseRuntimeConnectivity : public Gen2ArchRuntimeConnectivity
{
public:
    Gaudi3BaseRuntimeConnectivity(const int                   moduleId,
                                  const HCL_Comm              hclCommId,
                                  Gen2ArchServerConnectivity& serverConnectivity);
    virtual ~Gaudi3BaseRuntimeConnectivity() = default;

    const uint32_t                    getRemoteDevicePortMask(const uint32_t moduleId);
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
    const NicMacroIndexType getScaleupNicsMacrosCount() const { return m_scaleupNicsMacrosCount; }
    bool                    isRemoteScaleoutPort(const uint32_t remoteModuleId, const uint8_t remotePort) const;
    nics_mask_t getRemoteScaleOutPorts(const uint32_t remoteModuleId);  // Get a remote device scaleout ports

protected:
    virtual void initServerSpecifics() override;
    void         initNicMacros();
    void         initDeviceSetsAndDupMasks();
    void         initNicMacrosForAllDevices();

    RemoteDevicePortMasksArray m_remoteDevicePortMasks = {};
    NicMacroPairs              m_nicMacroPairs         = {};  // All the nic macros pairs of our device
    DevicesSet                 m_macroDevicesSet0;            // first set of module Ids that can be aggregated together
    DevicesSet                 m_macroDevicesSet1;  // second set of module Ids that can be aggregated together
    DeviceNicsMacrosMask       m_nicsMacrosDupMask      = {};
    NicMacrosDevicesArray      m_nicMacrosDevices       = {};
    NicMacroIndexType          m_scaleupNicsMacrosCount = 0;  // number of scaleup nic macros using dup mask
};
