#include "platform/gaudi3/port_mapping.h"

#include <cstdint>            // for uint8_t
#include <utility>            // for get
#include <type_traits>
#include <unordered_map>
#include <ostream>  // for operator<<, ostream
#include <unordered_set>

#include "platform/gen2_arch_common/types.h"  // for MAX_NICS_GEN2ARCH, GEN2ARCH_HLS_BOX_SIZE
#include "hcl_log_manager.h"  // log_*
#include "platform/gen2_arch_common/port_mapping_config.h"  // for Gen2ArchPortMappingConfig
#include "hcl_utils.h"  // for VERIFY
#include "platform/gaudi3/port_mapping_autogen.h"  // for g_gaudi3_card_location_*
#include "platform/gaudi3/port_mapping_autogen_hls3pcie.h"  // for g_hls3pcie_card_location*
#include "gaudi3/gaudi3.h"                         // for NIC_MAX_NUM_OF_MACROS
#include "platform/gaudi3/hal.h"                   // for Gaudi3Hal
#include "hcl_math_utils.h"                        // for div_round_up

const ServerNicsConnectivityArray g_HLS3NicsConnectivityArray = {g_gaudi3_card_location_0_mapping,
                                                                 g_gaudi3_card_location_1_mapping,
                                                                 g_gaudi3_card_location_2_mapping,
                                                                 g_gaudi3_card_location_3_mapping,
                                                                 g_gaudi3_card_location_4_mapping,
                                                                 g_gaudi3_card_location_5_mapping,
                                                                 g_gaudi3_card_location_6_mapping,
                                                                 g_gaudi3_card_location_7_mapping};

const ServerNicsConnectivityArray g_HLS3PcieNicsConnectivityArray = {g_hls3pcie_card_location_0_mapping,
                                                                     g_hls3pcie_card_location_1_mapping,
                                                                     g_hls3pcie_card_location_2_mapping,
                                                                     g_hls3pcie_card_location_3_mapping,
                                                                     g_hls3pcie_card_location_4_mapping,
                                                                     g_hls3pcie_card_location_5_mapping,
                                                                     g_hls3pcie_card_location_6_mapping,
                                                                     g_hls3pcie_card_location_7_mapping};

Gaudi3DevicePortMapping::Gaudi3DevicePortMapping(const int                          fd,
                                                 const Gen2ArchPortMappingConfig&   portMappingConfig,
                                                 const hcl::Gaudi3Hal&              hal,
                                                 const ServerNicsConnectivityArray& serverNicsConnectivityArray)
: Gen2ArchDevicePortMapping(fd), m_hal(hal)
{
    LOG_HCL_DEBUG(HCL, "Device ctor 1 called, hal.getDefaultBoxSize={}", hal.getDefaultBoxSize());
    init(serverNicsConnectivityArray, portMappingConfig);
}

Gaudi3DevicePortMapping::Gaudi3DevicePortMapping(const int             fd,
                                                 const hcl::Gaudi3Hal& hal,

                                                 const ServerNicsConnectivityArray& serverNicsConnectivityArray)
: Gen2ArchDevicePortMapping(fd), m_hal(hal)
{
    LOG_HCL_DEBUG(HCL, "Test ctor 2 called");
    Gen2ArchPortMappingConfig dummy_portMappingConfig;
    init(serverNicsConnectivityArray, dummy_portMappingConfig, false);
}

Gaudi3DevicePortMapping::Gaudi3DevicePortMapping(const int                          fd,
                                                 const int                          moduleId,
                                                 const hcl::Gaudi3Hal&              hal,
                                                 const ServerNicsConnectivityArray& serverNicsConnectivityArray)
: Gen2ArchDevicePortMapping(fd, moduleId), m_hal(hal)
{
    LOG_HCL_DEBUG(HCL, "Test ctor 3 called");
    Gen2ArchPortMappingConfig dummy_portMappingConfig;
    init(serverNicsConnectivityArray, dummy_portMappingConfig, false);
}

void Gaudi3DevicePortMapping::init(const ServerNicsConnectivityArray& serverNicsConnectivityArray,
                                   const Gen2ArchPortMappingConfig&   portMappingConfig,
                                   const bool                         setPortsMask)
{
    LOG_HCL_DEBUG(HCL, "Initializing");
    m_innerRanksPortMask.resize(DEFAULT_COMMUNICATORS_SIZE, 0);
    std::fill(m_remoteDevicePortMasks.begin(), m_remoteDevicePortMasks.end(), 0);

    // Keep the order of functions here
    assignDefaultMapping(serverNicsConnectivityArray);
    assignCustomMapping(portMappingConfig);
    logPortMappingConfig(m_spotlight_mappings[portMappingConfig.getSpotlightType()]);
    readMaxScaleOutPorts();
    if (setPortsMask)
    {
        setPortsMasks();
    }
    verifyPortsConfiguration(DEFAULT_SPOTLIGHT);  // DEFAULT_SPOTLIGHT can be used since it is verification only
    setNumScaleUpPorts();
    setNumScaleOutPorts();
    setMaxSubNics();
    initNicMacros();
    initDeviceSetsAndDupMasks();
    initNicMacrosForAllDevices();
}

void Gaudi3DevicePortMapping::onCommInit(HclDynamicCommunicator& dynamicComm)
{
    const HCL_Comm comm = dynamicComm;
    // resize if need
    if (comm >= m_innerRanksPortMask.size())
    {
        LOG_HCL_DEBUG(HCL, "Resizing m_innerRanksPortMask for new comm({})", comm);
        m_innerRanksPortMask.resize(m_innerRanksPortMask.size() + DEFAULT_COMMUNICATORS_SIZE, 0);
    }

    // calculate masks for new communicator
    for (const auto& scaleUpRank : dynamicComm.getInnerRanksExclusive())
    {
        const uint32_t moduleID = dynamicComm.getRemoteConnectionHeader(scaleUpRank).hwModuleID;
        m_innerRanksPortMask[comm] |= getRemoteDevicePortMask(moduleID, dynamicComm);
    }
    LOG_HCL_DEBUG(HCL, "m_innerRanksPortMask[{}] set to(0x{:x})", comm, m_innerRanksPortMask[comm]);
}

void Gaudi3DevicePortMapping::assignDefaultMapping()
{
    VERIFY(false, "Invalid call");
}

void Gaudi3DevicePortMapping::assignDefaultMapping(const ServerNicsConnectivityArray& serverNicsConnectivityArray)
{
    for (unsigned i = 0; i < MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH; i++)
    {
        for (unsigned moduleId = 0; moduleId < serverNicsConnectivityArray.size(); moduleId++)
        {
            LOG_HCL_DEBUG(HCL, "Assign spotlight={}, moduleId={}", i, moduleId);
            m_spotlight_mappings[i][moduleId] = serverNicsConnectivityArray[moduleId];
        }
    }
}

// calculate device port mask bits in order to speedup port mask calculation
const uint32_t Gaudi3DevicePortMapping::getRemoteDevicePortMask(uint32_t moduleId, HclDynamicCommunicator& dynamicComm)
{
    if (m_remoteDevicePortMasks[moduleId] == 0)
    {
        for (size_t portIndex = 0; portIndex < MAX_NICS_GEN2ARCH; ++portIndex)
        {
            const int remoteDevice = static_cast<uint32_t>(getRemoteDevice(portIndex, dynamicComm.getSpotlightType()));
            if ((remoteDevice >= 0) && (remoteDevice < GEN2ARCH_HLS_BOX_SIZE))
            {
                m_remoteDevicePortMasks[remoteDevice] |= (1u << portIndex);
            }
        }
    }

    return m_remoteDevicePortMasks[moduleId];
}

const uint32_t
Gaudi3DevicePortMapping::getDeviceToRemoteIndexPortMask(HclDynamicCommunicator& dynamicComm, box_devices_t& deviceToRemoteIndex)
{
    uint32_t portMask = 0;
    for (const auto& scaleUpRank : dynamicComm.getInnerRanksExclusive())
    {
        const uint32_t moduleID = dynamicComm.getRemoteConnectionHeader(scaleUpRank).hwModuleID;
        if (deviceToRemoteIndex[moduleID] != -1)
        {
            portMask |= getRemoteDevicePortMask(moduleID, dynamicComm);
        }
    }
    return portMask;
}

const uint32_t Gaudi3DevicePortMapping::getInnerRanksPortMask(HclDynamicCommunicator& dynamicComm)
{
    HCL_Comm comm = dynamicComm;
    LOG_HCL_TRACE(HCL, "m_innerRanksPortMask[{}] = (0x{:x})", comm, m_innerRanksPortMask[comm]);
    return m_innerRanksPortMask[comm];
}

const uint32_t Gaudi3DevicePortMapping::getRankToPortMask(const HCL_Rank rank, HclDynamicCommunicator& dynamicComm)
{
    const uint32_t moduleID = dynamicComm.getRemoteConnectionHeader(rank).hwModuleID;
    return getRemoteDevicePortMask(moduleID, dynamicComm);
}

unsigned Gaudi3DevicePortMapping::getDefaultScaleOutPortByIndex(unsigned idx) const
{
    return m_lkd_enabled_scaleout_ports(idx);
}

nics_mask_t Gaudi3DevicePortMapping::getRemoteScaleOutPorts(const uint32_t remoteModuleId,
                                                                      const unsigned spotlightType)
{
    nics_mask_t result;
    for (unsigned port_idx = 0; port_idx < MAX_NICS_GEN2ARCH; port_idx++)
    {
        if (isRemoteScaleoutPort(remoteModuleId, port_idx, spotlightType))
        {
            result.set(port_idx);
        }
    }
    return result;
}

bool Gaudi3DevicePortMapping::isRemoteScaleoutPort(const uint32_t remoteModuleId,
                                                   const uint8_t  remotePort,
                                                   const unsigned spotlightType) const
{
    return std::get<0>(m_spotlight_mappings[spotlightType][remoteModuleId][remotePort]) == SCALEOUT_DEVICE_ID;
}

unsigned Gaudi3DevicePortMapping::getRemoteSubPortIndex(const uint32_t remoteModuleId,
                                                        const uint8_t  remotePort,
                                                        const unsigned spotlightType) const
{
    return std::get<2>(m_spotlight_mappings[spotlightType][remoteModuleId][remotePort]);
}

void Gaudi3DevicePortMapping::assignCustomMapping(const Gen2ArchPortMappingConfig& portMappingConfig)
{
    if (!portMappingConfig.hasValidMapping()) return;
    // we will override the same spotlight that the user intended to (spotlight type is provided by the user, as part of
    // the configuration JSON file)
    m_spotlight_mappings[portMappingConfig.getSpotlightType()] = portMappingConfig.getMapping();  // copy entire mapping
    LOG_HCL_INFO(HCL, "Will be using custom mapping: {}.", portMappingConfig.getFilePathLoaded());
}

void Gaudi3DevicePortMapping::initNicMacros()
{
    LOG_HCL_DEBUG(HCL, "Calculating Nic Macros");

    // NIC_MAX_NUM_OF_MACROS
    constexpr size_t maxNicMacroPairs = NIC_MAX_NUM_OF_MACROS;
    LOG_HCL_TRACE(HCL, "maxNicMacroPairs={}", maxNicMacroPairs);

    for (NicMacroIndexType macroPairIndex = 0; macroPairIndex < maxNicMacroPairs; macroPairIndex++)
    {
        const uint16_t evenNic    = macroPairIndex * 2;
        const uint16_t oddNic     = evenNic + 1;
        const int      evenDevice = getRemoteDevice(evenNic);
        const int      oddDevice  = getRemoteDevice(oddNic);
        LOG_HCL_TRACE(HCL, "NIC_MACRO[{}]: evenDevice={}, oddDevice={}", macroPairIndex, evenDevice, oddDevice);

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

std::ostream& operator<<(std::ostream& os, const DevicesSet& devices)
{
    std::stringstream ss;
    std::copy(devices.begin(), devices.end(), std::ostream_iterator<decltype(*devices.begin())>(ss, ","));
    os << ss.str();
    return os;
}

void Gaudi3DevicePortMapping::initDeviceSetsAndDupMasks()
{
    LOG_HCL_DEBUG(HCL, "Calculating devices sets");
    // Determine which devices belong to set0 and set1 according to the port mapping nic macro pairs
    // We cannot aggregate devices that share the same nic macro
    const NicMacroPairs& nicMacroPairs(m_nicMacroPairs);
    DevicesSet           devicesProcessed     = {};
    NicMacroIndexType    macroIndex                = 0;  // This counts all the nic macros of our device
    NicMacroIndexType    nicMacroDupMaskIndex      = 0;  // This counts bits for scaleup nic macro's only
    NicMacroIndexType    nonScaleupNicsMacrosCount = 0;  // This counts nic macros of non-scaleup nics
    NicMacroIndexType    nonConnectedNicsMacrosCount = 0;  // This counts nic macros of not connected nics

    DevicesSet nonSharedDevices = {};  // Mark devices that are never shared with another to support HLS3PCIE
    m_scaleupNicsMacrosCount    = 0;   // Clear scaleout only nic macros count
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
                // 1 or 2 disconnected nics - no scaleup
                nonScaleupNicsMacrosCount++;
                nonConnectedNicsMacrosCount++;
                nicMacroDupMaskIndex++;
                break;
            case NIC_MACRO_NO_SCALEUP_NICS:
                // All scaleout nics, skip it in counting
                nonScaleupNicsMacrosCount++;
                break;
            case NIC_MACRO_SINGLE_SCALEUP_NIC:
                // A single device that is sharing it with a scaleout/not connected nic, add it to first set
                VERIFY(m_macroDevicesSet1.count(nicMacroPair.m_device0) == 0);  // This device it cant be in other set1
                devicesProcessed.insert(nicMacroPair.m_device0);
                m_macroDevicesSet0.insert(nicMacroPair.m_device0);
                nonSharedDevices.erase(nicMacroPair.m_device0);
                m_nicsMacrosDupMask[nicMacroPair.m_device0] =
                    m_nicsMacrosDupMask[nicMacroPair.m_device0] |
                    (1 << nicMacroDupMaskIndex);  // Set the NIC macro bit for first device
                nicMacroDupMaskIndex++;
                break;
            case NIC_MACRO_TWO_SCALEUP_NICS:  // nic macro with 2 scaleup nics
                m_nicsMacrosDupMask[nicMacroPair.m_device0] =
                    m_nicsMacrosDupMask[nicMacroPair.m_device0] |
                    (1 << nicMacroDupMaskIndex);  // Set the NIC macro bit for first device
                devicesProcessed.insert(nicMacroPair.m_device0);
                if (nicMacroPair.m_device0 != nicMacroPair.m_device1)
                {
                    nonSharedDevices.erase(nicMacroPair.m_device0);
                    nonSharedDevices.erase(nicMacroPair.m_device1);
                    // 2 different devices, put first in first set and 2nd in 2nd set
                    VERIFY(m_macroDevicesSet1.count(nicMacroPair.m_device0) ==
                           0);  // This device cant be in the other set1
                    VERIFY(m_macroDevicesSet0.count(nicMacroPair.m_device1) ==
                           0);  // This device cant be in the other set0
                    devicesProcessed.insert(nicMacroPair.m_device1);
                    m_macroDevicesSet0.insert(nicMacroPair.m_device0);  // Device will be put in set0
                    m_macroDevicesSet1.insert(nicMacroPair.m_device1);  // Device will be put in set1
                    m_nicsMacrosDupMask[nicMacroPair.m_device1] =
                        m_nicsMacrosDupMask[nicMacroPair.m_device1] |
                        (1 << nicMacroDupMaskIndex);  // Set the NIC macro bit for 2nd device
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
                    m_nicsMacrosDupMask[nicMacroPair.m_device0] =
                        m_nicsMacrosDupMask[nicMacroPair.m_device0] |
                        (1 << nicMacroDupMaskIndex);  // Set the NIC macro bit for 2nd device
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
        m_macroDevicesSet0.insert(deviceId);              // Device will be put in set0
        VERIFY(m_macroDevicesSet1.count(deviceId) == 0);  // This device cant be in the other set1
    }

    LOG_HCL_TRACE(HCL,
                  "devicesProcessed={}, nonScaleupNicsMacrosCount={}, nonConnectedNicsMacrosCount={}",
                  devicesProcessed,
                  nonScaleupNicsMacrosCount,
                  nonConnectedNicsMacrosCount);
    VERIFY(macroIndex - nonScaleupNicsMacrosCount + nonConnectedNicsMacrosCount == nicMacroDupMaskIndex,
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
        const unsigned maxDupMaskBits = div_round_up(m_hal.getMaxNumScaleUpPortsPerConnection(), 2);
        LOG_HCL_DEBUG(HCL, "maxDupMaskBits={}, m_nicsMacrosDupMask[{}]={:012b}", maxDupMaskBits, index++, dupMask);
        const std::bitset<NIC_MAX_NUM_OF_MACROS> dupMaskBitSet(dupMask);
        VERIFY(dupMaskBitSet.count() == maxDupMaskBits || dupMaskBitSet.count() == 0,
               "device {} dupMask {:012b} must have 0 or {}} bits set",
               index,
               dupMask,
               maxDupMaskBits);
    }
}

void Gaudi3DevicePortMapping::initNicMacrosForAllDevices()
{
    for (size_t deviceId = 0; deviceId < m_nicMacrosDevices.size(); deviceId++)
    {
        // Each device belongs to 2 or more NIC macros, find out which
        const uint16_t mask = m_nicsMacrosDupMask[deviceId];
        std::unordered_set<uint32_t> macrosIndexesSet;  // store here the nic macro indexes
        if (mask)                // skip self device
        {
            for (NicMacroIndexType macroPairIndex = 0; macroPairIndex < NIC_MAX_NUM_OF_MACROS; macroPairIndex++)
            {
                if (mask & (1 << macroPairIndex))
                {
                    macrosIndexesSet.insert(macroPairIndex);
                }
            }
            const unsigned numNicMacros = div_round_up(m_hal.getMaxNumScaleUpPortsPerConnection(), 2);
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
