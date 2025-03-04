#pragma once

#include <cstdint>  // for uint8_t

#include "platform/gen2_arch_common/server_connectivity.h"        // for Gen2ArchServerConnectivity
#include "platform/gen2_arch_common/runtime_connectivity.h"       // for Gen2ArchRuntimeConnectivity
#include "platform/gen2_arch_common/server_connectivity_types.h"  // for DEFAULT_COMM_ID
#include "platform/gaudi3/nic_macro_types.h"
#include "platform/gaudi3/gaudi3_base_server_connectivity.h"   // for Gaudi3BaseServerConnectivity
#include "platform/gaudi3/gaudi3_base_runtime_connectivity.h"  // for Gaudi3BaseRuntimeConnectivity
#include "platform/gen2_arch_common/types.h"                   // for box_devices_t
#include "hcl_bits.h"                                          // for nics_mask_t
#include "platform/gen2_arch_common/hcl_device_config.h"       // for HclDeviceConfig

// forward decl
class HclDynamicCommunicator;

// Abstract class for Gaudi3 based servers (HLS3, HLS3PCIE) with nics macros handling

class Gaudi3BaseServerConnectivity : public Gen2ArchServerConnectivity
{
public:
    Gaudi3BaseServerConnectivity(const int                          fd,
                                 const int                          moduleId,
                                 const bool                         useDummyConnectivity,
                                 const ServerNicsConnectivityArray& serverNicsConnectivityArray,
                                 HclDeviceConfig&                   deviceConfig);
    virtual ~Gaudi3BaseServerConnectivity() = default;

    virtual void onCommInit(HclDynamicCommunicator& dynamicComm) override;

    // Get all comm inner ranks ports mask
    const uint32_t getInnerRanksPortMask(const HclDynamicCommunicator& dynamicComm) const;
    // Get specific rank scaleup ports mask
    const uint32_t getRankToPortMask(const HCL_Rank rank, HclDynamicCommunicator& dynamicComm);
    // Get all remote devices ports mask
    const RemoteDevicePortMasksArray& getRemoteDevicesPortMasks(const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;
    // Get nic macros mask for remote device
    uint16_t getNicsMacrosDupMask(const uint32_t remoteDevice, const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;
    // Get nic macros vector for remote device
    const NicMacrosPerDevice& getNicMacrosPerDevice(const uint32_t remoteDevice,
                                                    const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;
    // Get all device in first or second scaleup group that do not share nic macros
    const DevicesSet& getDevicesSet(const bool first, const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;
    // Get all nic macros count for all scaleup ranks
    const NicMacroIndexType getScaleupNicsMacrosCount(const HCL_Comm hclCommId = DEFAULT_COMM_ID) const;
    // Get a remote device scaleout ports
    nics_mask_t getRemoteScaleOutPorts(const uint32_t remoteModuleId, const HCL_Comm hclCommId = DEFAULT_COMM_ID);
    // Given an array with some non-negative offset per remote scaleup device, non-participating ranks get "-1"
    // The port mask returned should be for all ranks that are not -1
    const uint32_t getDeviceToRemoteIndexPortMask(HclDynamicCommunicator& dynamicComm,
                                                  const box_devices_t&    deviceToRemoteIndex);

protected:
    Gaudi3BaseRuntimeConnectivity& getGaudi3BasedRunTimeConnectivity([[maybe_unused]] const HCL_Comm hclCommId)
    {
        return (*(dynamic_cast<Gaudi3BaseRuntimeConnectivity*>(m_commsRuntimeConnectivity[DEFAULT_COMM_ID].get())));
    };

    const Gaudi3BaseRuntimeConnectivity&
    getGaudi3BasedRunTimeConnectivityConst([[maybe_unused]] const HCL_Comm hclCommId) const
    {
        return (
            *(dynamic_cast<const Gaudi3BaseRuntimeConnectivity*>(m_commsRuntimeConnectivity[DEFAULT_COMM_ID].get())));
    };

    const uint32_t getRemoteDevicePortMask(const uint32_t moduleId, HclDynamicCommunicator& dynamicComm);
    bool isRemoteScaleoutPort(const uint32_t remoteModuleId, const uint8_t remotePort, const HCL_Comm hclCommId) const;

    std::vector<uint64_t> m_innerRanksPortMask = {};  // Per comm, save the inner ranks port mask

private:
};
