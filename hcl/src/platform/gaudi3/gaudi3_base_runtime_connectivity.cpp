#include "platform/gaudi3/gaudi3_base_runtime_connectivity.h"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint*_t
#include <bitset>
#include <unordered_set>

#include "gaudi3/gaudi3.h"                                        // for NIC_MAX_NUM_OF_MACROS
#include "platform/gen2_arch_common/runtime_connectivity.h"       // for Gen2ArchRuntimeConnectivity
#include "platform/gen2_arch_common/server_connectivity_types.h"  // for
#include "platform/gaudi3/gaudi3_base_server_connectivity.h"      // for Gaudi3BaseServerConnectivity

#include "hcl_dynamic_communicator.h"  // for HclDynamicCommunicator
#include "hcl_bits.h"                  // for nics_mask_t
#include "hcl_math_utils.h"            // for div_round_up
#include "hcl_utils.h"                 // for VERIFY
#include "hcl_log_manager.h"           // for LOG_*

Gaudi3BaseRuntimeConnectivity::Gaudi3BaseRuntimeConnectivity(const int                   moduleId,
                                                             const HCL_Comm              hclCommId,
                                                             Gen2ArchServerConnectivity& serverConnectivity)
: Gen2ArchRuntimeConnectivity(moduleId, hclCommId, serverConnectivity)
{
}

void Gaudi3BaseRuntimeConnectivity::initServerSpecifics()
{
    LOG_HCL_DEBUG(HCL, "m_hclCommId={}", m_hclCommId);
    initNicMacros();
    initDeviceSetsAndDupMasks();
    initNicMacrosForAllDevices();
}

// calculate device port mask bits in order to speedup port mask calculation
const uint32_t Gaudi3BaseRuntimeConnectivity::getRemoteDevicePortMask(const uint32_t moduleId)
{
    if (m_remoteDevicePortMasks[moduleId] == 0)
    {
        for (uint16_t portIndex = 0; portIndex < MAX_NICS_GEN2ARCH; ++portIndex)
        {
            const uint32_t remoteDevice = static_cast<uint32_t>(getRemoteDevice(portIndex));
            if (remoteDevice < GEN2ARCH_HLS_BOX_SIZE)
            {
                m_remoteDevicePortMasks[remoteDevice] |= (1u << portIndex);
            }
        }
        LOG_HCL_DEBUG(HCL,
                      "m_hclCommId={}, m_remoteDevicePortMasks[{}]={:024b}",
                      m_hclCommId,
                      moduleId,
                      m_remoteDevicePortMasks[moduleId]);
    }

    return m_remoteDevicePortMasks[moduleId];
}

bool Gaudi3BaseRuntimeConnectivity::isRemoteScaleoutPort(const uint32_t remoteModuleId, const uint8_t remotePort) const
{
    return std::get<0>(m_mappings[remoteModuleId][remotePort]) == SCALEOUT_DEVICE_ID;
}

nics_mask_t Gaudi3BaseRuntimeConnectivity::getRemoteScaleOutPorts(const uint32_t remoteModuleId)
{
    nics_mask_t result;
    for (uint16_t port_idx = 0; port_idx < MAX_NICS_GEN2ARCH; port_idx++)
    {
        if (isRemoteScaleoutPort(remoteModuleId, port_idx))
        {
            result.set(port_idx);
        }
    }
    return result;
}

void Gaudi3BaseRuntimeConnectivity::initNicMacros()
{
    LOG_HCL_DEBUG(HCL, "Calculating Nic Macros, m_allPorts={:024b}", (uint64_t)(m_allPorts));

    constexpr size_t maxNicMacroPairs = NIC_MAX_NUM_OF_MACROS;
    LOG_HCL_TRACE(HCL, "maxNicMacroPairs={}", maxNicMacroPairs);

    for (NicMacroIndexType macroPairIndex = 0; macroPairIndex < maxNicMacroPairs; macroPairIndex++)
    {
        const uint16_t evenNic     = macroPairIndex * 2;
        const uint16_t oddNic      = evenNic + 1;
        const bool     evenEnabled = m_allPorts[evenNic];
        const bool     oddEnabled  = m_allPorts[oddNic];
        const int      evenDevice  = getRemoteDevice(evenNic);
        const int      oddDevice   = getRemoteDevice(oddNic);
        LOG_HCL_TRACE(HCL,
                      "NIC_MACRO[{}]: evenDevice={}, oddDevice={}, evenEnabled={}, oddEnabled={}",
                      macroPairIndex,
                      evenDevice,
                      oddDevice,
                      evenEnabled,
                      oddEnabled);

        DevicesSet devicePair;
        if (evenDevice >= 0)
        {
            VERIFY(m_moduleId != evenDevice,
                   "Invalid even nic remote device module id in ports configuration, m_moduleId={}, macroPairIndex={}, "
                   "evenDevice={}",
                   m_moduleId,
                   macroPairIndex,
                   evenDevice);
            devicePair.insert(evenDevice);
        }

        if (oddDevice >= 0)
        {
            VERIFY(m_moduleId != oddDevice,
                   "Invalid odd nic remote device module id in ports configuration, m_moduleId={}, macroPairIndex={}, "
                   "oddDevice={}",
                   m_moduleId,
                   macroPairIndex,
                   oddDevice);
            devicePair.insert(oddDevice);
        }

        VERIFY(devicePair.size() <= 2, "devicePair.size {} must be <= 2", devicePair.size());
        NicMacroPair nicMacroPair;
        if (devicePair.size() == 0)
        {
            if (((unsigned)evenDevice == NOT_CONNECTED_DEVICE_ID) || ((unsigned)oddDevice == NOT_CONNECTED_DEVICE_ID))
            {
                nicMacroPair.m_nicsConfig =
                    NIC_MACRO_NOT_CONNECTED_NICS;  // no connected nics in this macro or  1 scaleout nic
            }
            else
            {
                nicMacroPair.m_nicsConfig = NIC_MACRO_NO_SCALEUP_NICS;  // all scaleout nics in this macro
            }
        }
        else if (devicePair.size() == 1)  // even or odd nic had device, check 2nd device
        {
            if (evenDevice == oddDevice)  // same device on both nics
            {
                VERIFY((unsigned)evenDevice != SCALEOUT_DEVICE_ID,
                       "Invalid remote device config, macroPairIndex={}, evenDevice={}, oddDevice={}",
                       macroPairIndex,
                       evenDevice,
                       oddDevice);
                nicMacroPair.m_nicsConfig = NIC_MACRO_TWO_SCALEUP_NICS;
                nicMacroPair.m_device0    = evenDevice;
                nicMacroPair.m_device1    = evenDevice;
            }
            else  // either even or odd nic are scaleup and the other is scaleout or not connected
            {
                nicMacroPair.m_nicsConfig = NIC_MACRO_SINGLE_SCALEUP_NIC;
                nicMacroPair.m_device0    = *devicePair.begin();
            }
        }
        else  // 2 nics to 2 different devices
        {
            VERIFY(((unsigned)evenDevice != SCALEOUT_DEVICE_ID) && ((unsigned)oddDevice != SCALEOUT_DEVICE_ID),
                   "Invalid remote device config, macroPairIndex={}, evenDevice={}, oddDevice={}",
                   macroPairIndex,
                   evenDevice,
                   oddDevice);
            nicMacroPair.m_device0    = evenDevice;
            nicMacroPair.m_device1    = oddDevice;
            nicMacroPair.m_nicsConfig = NIC_MACRO_TWO_SCALEUP_NICS;
        }
        LOG_HCL_TRACE(HCL,
                      "Added m_nicMacroPairs[{}]: m_nicsConfig={}, m_device0={}, m_device1={}",
                      macroPairIndex,
                      nicMacroPair.m_nicsConfig,
                      nicMacroPair.m_device0,
                      nicMacroPair.m_device1);
        m_nicMacroPairs[macroPairIndex] = nicMacroPair;
    }
}

void Gaudi3BaseRuntimeConnectivity::initDeviceSetsAndDupMasks()
{
    LOG_HCL_DEBUG(HCL, "Calculating devices sets");
    // Determine which devices belong to set0 and set1 according to the port mapping nic macro pairs
    // We cannot aggregate devices that share the same nic macro
    const NicMacroPairs& nicMacroPairs(m_nicMacroPairs);
    DevicesSet           devicesProcessed            = {};
    NicMacroIndexType    macroIndex                  = 0;  // This counts all the nic macros of our device
    NicMacroIndexType    nicMacroDupMaskIndex        = 0;  // This counts bits for scaleup nic macro's only
    NicMacroIndexType    nonScaleupNicsMacrosCount   = 0;  // This counts nic macros of non-scaleup nics
    NicMacroIndexType    nonConnectedNicsMacrosCount = 0;  // This counts nic macros of not connected nics

    // Mark devices that are never shared with another to support HLS3PCIE
    DevicesSet nonSharedDevices = {};
    // Clear scaleout only nic macros count
    m_scaleupNicsMacrosCount = 0;
    for (const NicMacroPair& nicMacroPair : nicMacroPairs)
    {
        LOG_HCL_TRACE(HCL,
                      "macroIndex={}, nicMacroDupMaskIndex={}, nicMacroPair.m_nicsConfig={}, "
                      "nicMacroPair.m_device0={}, "
                      "nicMacroPair.m_device1={}",
                      macroIndex,
                      nicMacroDupMaskIndex,
                      nicMacroPair.m_nicsConfig,
                      nicMacroPair.m_device0,
                      nicMacroPair.m_device1);
        LOG_HCL_TRACE(HCL,
                      "m_macroDevicesSet0={}, m_macroDevicesSet1={}, nonSharedDevices={}",
                      m_macroDevicesSet0,
                      m_macroDevicesSet1,
                      nonSharedDevices);
        switch (nicMacroPair.m_nicsConfig)
        {
            case NIC_MACRO_NOT_CONNECTED_NICS:
                // 1 or 2 disconnected nics - no scaleup. Do not include it in the nic macros dup mask
                nonConnectedNicsMacrosCount++;
                break;
            case NIC_MACRO_NO_SCALEUP_NICS:
                // All scaleout nics, skip it in counting
                nonScaleupNicsMacrosCount++;
                break;
            case NIC_MACRO_SINGLE_SCALEUP_NIC:
                // A single device that is sharing it with a scaleout/not connected nic, add it to first set
                // This device it cant be in other set1
                VERIFY(m_macroDevicesSet1.count(nicMacroPair.m_device0) == 0);
                devicesProcessed.insert(nicMacroPair.m_device0);
                m_macroDevicesSet0.insert(nicMacroPair.m_device0);
                nonSharedDevices.erase(nicMacroPair.m_device0);
                // Set the NIC macro bit for first device
                m_nicsMacrosDupMask[nicMacroPair.m_device0] =
                    m_nicsMacrosDupMask[nicMacroPair.m_device0] | (1 << nicMacroDupMaskIndex);
                nicMacroDupMaskIndex++;
                break;
            case NIC_MACRO_TWO_SCALEUP_NICS:  // nic macro with 2 scaleup nics
                // Set the NIC macro bit for first device
                m_nicsMacrosDupMask[nicMacroPair.m_device0] =
                    m_nicsMacrosDupMask[nicMacroPair.m_device0] | (1 << nicMacroDupMaskIndex);
                devicesProcessed.insert(nicMacroPair.m_device0);
                if (nicMacroPair.m_device0 != nicMacroPair.m_device1)
                {
                    nonSharedDevices.erase(nicMacroPair.m_device0);
                    nonSharedDevices.erase(nicMacroPair.m_device1);
                    // 2 different devices, put first in first set and 2nd in 2nd set
                    // This device cant be in the other set1
                    VERIFY(m_macroDevicesSet1.count(nicMacroPair.m_device0) == 0);
                    // This device cant be in the other set0
                    VERIFY(m_macroDevicesSet0.count(nicMacroPair.m_device1) == 0);
                    devicesProcessed.insert(nicMacroPair.m_device1);
                    // Device will be put in set0
                    m_macroDevicesSet0.insert(nicMacroPair.m_device0);
                    // Device will be put in set1
                    m_macroDevicesSet1.insert(nicMacroPair.m_device1);
                    // Set the NIC macro bit for 2nd device
                    m_nicsMacrosDupMask[nicMacroPair.m_device1] =
                        m_nicsMacrosDupMask[nicMacroPair.m_device1] | (1 << nicMacroDupMaskIndex);
                    nicMacroDupMaskIndex++;
                }
                else
                {
                    // Same device on both nics - skip set setting, it will be added on a shared nic macro with another
                    // device, but set NIC macro bit
                    // Handle case for HLS3PCIE - no shared nic macros, so we need to set them after this loop
                    if ((m_macroDevicesSet1.count(nicMacroPair.m_device0) == 0) &&
                        (m_macroDevicesSet1.count(nicMacroPair.m_device0) == 0))  // device was never shared before
                    {
                        nonSharedDevices.insert(nicMacroPair.m_device0);
                    }
                    // Set the NIC macro bit for 2nd device
                    m_nicsMacrosDupMask[nicMacroPair.m_device0] =
                        m_nicsMacrosDupMask[nicMacroPair.m_device0] | (1 << nicMacroDupMaskIndex);
                    nicMacroDupMaskIndex++;
                }
                break;
        }
        macroIndex++;
    }

    // Handle cases where a device is never in a shared macro (HLS3PCIE) - just push device into first set, it should
    // not be in 2nd set
    for (const HCL_HwModuleId deviceId : nonSharedDevices)
    {
        LOG_HCL_TRACE(HCL, "Adding left over device {} to m_macroDevicesSet0", deviceId);
        // Device will be put in set0
        m_macroDevicesSet0.insert(deviceId);
        // This device cant be in the other set1
        VERIFY(m_macroDevicesSet1.count(deviceId) == 0);
    }

    LOG_HCL_TRACE(HCL,
                  "devicesProcessed={}, nonScaleupNicsMacrosCount={}, nonConnectedNicsMacrosCount={}",
                  devicesProcessed,
                  nonScaleupNicsMacrosCount,
                  nonConnectedNicsMacrosCount);
    // nicMacroDupMaskIndex will have dup mask set per active nic macro. It can be smaller than max nic macros
    // When no connected nics are present.
    VERIFY(macroIndex - nonScaleupNicsMacrosCount - nonConnectedNicsMacrosCount == nicMacroDupMaskIndex,
           "Wrong number of scaleup nic macros nicMacroDupMaskIndex={}, nonScaleupNicsMacrosCount={}, "
           "nonConnectedNicsMacrosCount={}, macroIndex={}",
           nicMacroDupMaskIndex,
           nonScaleupNicsMacrosCount,
           nonConnectedNicsMacrosCount,
           macroIndex);

    m_scaleupNicsMacrosCount = nicMacroDupMaskIndex;
    LOG_HCL_DEBUG(HCL,
                  "m_macroDevicesSet0={}, m_macroDevicesSet1={}, m_scaleupNicsMacrosCount={}",
                  m_macroDevicesSet0,
                  m_macroDevicesSet1,
                  m_scaleupNicsMacrosCount);

    size_t index = 0;
    for (const uint16_t dupMask : m_nicsMacrosDupMask)
    {
        const unsigned maxDupMaskBits = div_round_up(getMaxNumScaleUpPortsPerConnection(), 2);
        LOG_HCL_DEBUG(HCL, "maxDupMaskBits={}, m_nicsMacrosDupMask[{}]={:012b}", maxDupMaskBits, index++, dupMask);
        const std::bitset<NIC_MAX_NUM_OF_MACROS> dupMaskBitSet(dupMask);
        VERIFY(dupMaskBitSet.count() == maxDupMaskBits || dupMaskBitSet.count() == 0,
               "device {} dupMask {:012b} must have 0 or {}} bits set",
               index,
               dupMask,
               maxDupMaskBits);
    }
}

void Gaudi3BaseRuntimeConnectivity::initNicMacrosForAllDevices()
{
    LOG_HCL_DEBUG(HCL, "Started");
    for (size_t deviceId = 0; deviceId < m_nicMacrosDevices.size(); deviceId++)
    {
        // Each device belongs to 2 or more NIC macros, find out which
        const uint16_t               mask = m_nicsMacrosDupMask[deviceId];
        std::unordered_set<uint32_t> macrosIndexesSet;  // store here the nic macro indexes
        if (mask)                                       // skip self device
        {
            for (NicMacroIndexType macroPairIndex = 0; macroPairIndex < NIC_MAX_NUM_OF_MACROS; macroPairIndex++)
            {
                if (mask & (1 << macroPairIndex))
                {
                    macrosIndexesSet.insert(macroPairIndex);
                }
            }
            const unsigned numNicMacros = div_round_up(getMaxNumScaleUpPortsPerConnection(), 2);
            LOG_HCL_DEBUG(HCL, "numNicMacros={}", numNicMacros);
            VERIFY(macrosIndexesSet.size() == numNicMacros,
                   "Cannot find {} nic macros for deviceId={}, mask={:012b}, found {}",
                   numNicMacros,
                   deviceId,
                   mask,
                   macrosIndexesSet.size());
            m_nicMacrosDevices[deviceId].clear();
            std::copy(macrosIndexesSet.begin(),
                      macrosIndexesSet.end(),
                      std::back_inserter(m_nicMacrosDevices[deviceId]));
            LOG_HCL_TRACE(HCL, "Adding deviceId={}, macros={}", deviceId, m_nicMacrosDevices[deviceId].size());
        }
    }
}
