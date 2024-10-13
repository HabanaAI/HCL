#include "platform/gaudi3/gaudi3_base_server_connectivity.h"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint*_t

#include "platform/gen2_arch_common/server_connectivity_types.h"  // for ServerNicsConnectivityArray
#include "platform/gaudi3/gaudi3_base_runtime_connectivity.h"     // for Gaudi3BaseRuntimeConnectivity
#include "hcl_dynamic_communicator.h"                             // for HclDynamicCommunicator
#include "hcl_utils.h"                                            // for LOG_HCL_*
#include "hcl_log_manager.h"                                      // for LOG_*
#include "platform/gen2_arch_common/types.h"                      // for box_devices_t
#include "platform/gaudi3/nic_macro_types.h"

Gaudi3BaseServerConnectivity::Gaudi3BaseServerConnectivity(
    const int                          fd,
    const int                          moduleId,
    const bool                         useDummyConnectivity,
    const ServerNicsConnectivityArray& serverNicsConnectivityArray,
    HclDeviceConfig&                   deviceConfig)
: Gen2ArchServerConnectivity(fd, moduleId, useDummyConnectivity, serverNicsConnectivityArray, deviceConfig)
{
}

void Gaudi3BaseServerConnectivity::onCommInit(HclDynamicCommunicator& dynamicComm)
{
    const HCL_Comm hclCommId = dynamicComm;
    // resize if need
    if (hclCommId >= m_innerRanksPortMask.size())
    {
        LOG_HCL_DEBUG(HCL, "Resizing m_innerRanksPortMask for new comm({})", hclCommId);
        m_innerRanksPortMask.resize(m_innerRanksPortMask.size() + DEFAULT_COMMUNICATORS_SIZE, 0);
    }

    // calculate masks for new communicator
    for (const auto& scaleUpRank : dynamicComm.getInnerRanksExclusive())
    {
        const uint32_t moduleID = dynamicComm.getRemoteConnectionHeader(scaleUpRank).hwModuleID;
        m_innerRanksPortMask[hclCommId] |=
            getGaudi3BasedRunTimeConnectivity(hclCommId).getRemoteDevicePortMask(moduleID);
    }
    LOG_HCL_DEBUG(HCL, "m_innerRanksPortMask[{}] set to ({:024b})", hclCommId, m_innerRanksPortMask[hclCommId]);
}

const uint32_t Gaudi3BaseServerConnectivity::getInnerRanksPortMask(const HclDynamicCommunicator& dynamicComm) const
{
    const HCL_Comm hclCommId = dynamicComm;
    return m_innerRanksPortMask[hclCommId];
}

const uint32_t Gaudi3BaseServerConnectivity::getRankToPortMask(const HCL_Rank rank, HclDynamicCommunicator& dynamicComm)
{
    const uint32_t moduleID  = dynamicComm.getRemoteConnectionHeader(rank).hwModuleID;
    const HCL_Comm hclCommId = dynamicComm;
    return getGaudi3BasedRunTimeConnectivity(hclCommId).getRemoteDevicePortMask(moduleID);
}

const RemoteDevicePortMasksArray&
Gaudi3BaseServerConnectivity::getRemoteDevicesPortMasks(const HCL_Comm hclCommId) const
{
    return getGaudi3BasedRunTimeConnectivityConst(hclCommId).getRemoteDevicesPortMasks();
}

uint16_t Gaudi3BaseServerConnectivity::getNicsMacrosDupMask(const uint32_t remoteDevice, const HCL_Comm hclCommId) const
{
    return getGaudi3BasedRunTimeConnectivityConst(hclCommId).getNicsMacrosDupMask(remoteDevice);
}

const NicMacrosPerDevice& Gaudi3BaseServerConnectivity::getNicMacrosPerDevice(const uint32_t remoteDevice,
                                                                              const HCL_Comm hclCommId) const
{
    return getGaudi3BasedRunTimeConnectivityConst(hclCommId).getNicMacrosPerDevice(remoteDevice);
}

const DevicesSet& Gaudi3BaseServerConnectivity::getDevicesSet(const bool first, const HCL_Comm hclCommId) const
{
    return getGaudi3BasedRunTimeConnectivityConst(hclCommId).getDevicesSet(first);
}

const NicMacroIndexType Gaudi3BaseServerConnectivity::getScaleupNicsMacrosCount(const HCL_Comm hclCommId) const
{
    return getGaudi3BasedRunTimeConnectivityConst(hclCommId).getScaleupNicsMacrosCount();
}

nics_mask_t Gaudi3BaseServerConnectivity::getRemoteScaleOutPorts(const uint32_t remoteModuleId,
                                                                 const HCL_Comm hclCommId)
{
    return getGaudi3BasedRunTimeConnectivity(hclCommId).getRemoteScaleOutPorts(remoteModuleId);
}

const uint32_t Gaudi3BaseServerConnectivity::getDeviceToRemoteIndexPortMask(HclDynamicCommunicator& dynamicComm,
                                                                            const box_devices_t&    deviceToRemoteIndex)
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

const uint32_t Gaudi3BaseServerConnectivity::getRemoteDevicePortMask(const uint32_t          moduleId,
                                                                     HclDynamicCommunicator& dynamicComm)
{
    const HCL_Comm hclCommId = dynamicComm;
    return getGaudi3BasedRunTimeConnectivity(hclCommId).getRemoteDevicePortMask(moduleId);
}

bool Gaudi3BaseServerConnectivity::isRemoteScaleoutPort(const uint32_t remoteModuleId,
                                                        const uint8_t  remotePort,
                                                        const HCL_Comm hclCommId) const
{
    return getGaudi3BasedRunTimeConnectivityConst(hclCommId).isRemoteScaleoutPort(remoteModuleId, remotePort);
}

std::ostream& operator<<(std::ostream& os, const DevicesSet& devices)
{
    std::stringstream ss;
    std::copy(devices.begin(), devices.end(), std::ostream_iterator<decltype(*devices.begin())>(ss, ","));
    os << ss.str();
    return os;
}
