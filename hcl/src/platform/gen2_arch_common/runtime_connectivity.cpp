#include "platform/gen2_arch_common/runtime_connectivity.h"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint*_t
#include <memory>   // for allocator_traits<>::value_type

#include "synapse_common_types.h"             // for synDeviceType
#include "platform/gen2_arch_common/types.h"  // for HCL_INVALID_PORT, MAX_NICS_GEN2ARCH
#include "ibverbs/hcl_ibverbs.h"              // for hcl_ibverbs_t

#include "platform/gen2_arch_common/server_connectivity_types.h"        // for ServerNicsConnectivityArray
#include "platform/gen2_arch_common/server_connectivity_user_config.h"  // for ServerConnectivityUserConfig
#include "platform/gen2_arch_common/server_connectivity.h"              // for Gen2ArchServerConnectivity
#include "platform/gen2_arch_common/runtime_connectivity.h"             // for Gen2ArchRuntimeConnectivity

#include "hcl_utils.h"        // for VERIFY
#include "hcl_log_manager.h"  // for LOG_*

Gen2ArchRuntimeConnectivity::Gen2ArchRuntimeConnectivity(const int                   moduleId,
                                                         const HCL_Comm              hclCommId,
                                                         Gen2ArchServerConnectivity& serverConnectivity)
: m_moduleId(moduleId), m_hclCommId(hclCommId), m_serverConnectivity(serverConnectivity)
{
    LOG_HCL_DEBUG(HCL, "m_moduleId={}, hclCommId={}", m_moduleId, hclCommId);
}

void Gen2ArchRuntimeConnectivity::logPortMappingConfig(const ServerNicsConnectivityArray& mapping)
{
    unsigned deviceIndex = 0;
    for (auto& device : mapping)
    {
        unsigned nicIndex = 0;
        for (auto& tuple : device)
        {
            const int     remoteDeviceId = std::get<0>(tuple);
            const uint8_t remoteNicId    = std::get<1>(tuple);
            const uint8_t remoteSubNicId = std::get<2>(tuple);
            LOG_TRACE(HCL,
                      "m_hclCommId={} Mapping: [{}][{}] = [[ {}, {}, {} ]]",
                      m_hclCommId,
                      deviceIndex,
                      nicIndex,
                      remoteDeviceId,
                      remoteNicId,
                      remoteSubNicId);
            nicIndex++;
        }
        deviceIndex++;
    }
}

void Gen2ArchRuntimeConnectivity::init(const ServerNicsConnectivityArray&  serverNicsConnectivityArray,
                                       const ServerConnectivityUserConfig& usersConnectivityConfig,
                                       const bool                          readLkdPortsMask)
{
    LOG_HCL_DEBUG(HCL, "Started, m_hclCommId={}, readLkdPortsMask={}", m_hclCommId, readLkdPortsMask);

    // Keep the order of functions here
    assignDefaultMapping(serverNicsConnectivityArray);
    assignCustomMapping(usersConnectivityConfig);
    logPortMappingConfig(m_mappings);
    readAllPorts();
    // In case of unit test, init some vars with defaults for parent class
    if (!readLkdPortsMask)
    {
        m_serverConnectivity.setUnitTestsPortsMasks(m_fullScaleoutPorts, m_allPorts);
    }
    setPortsMasksGlbl();
    verifyPortsConfiguration();
    setNumScaleUpPorts();
    setNumScaleOutPortsGlbl();
    setMaxSubNics();
    initServerSpecifics();
}

void Gen2ArchRuntimeConnectivity::assignDefaultMapping(const ServerNicsConnectivityArray& serverNicsConnectivityArray)
{
    for (unsigned moduleId = 0; moduleId < serverNicsConnectivityArray.size(); moduleId++)
    {
        LOG_HCL_DEBUG(HCL, "Assign m_hclCommId={}, moduleId={}", m_hclCommId, moduleId);
        m_mappings[moduleId] = serverNicsConnectivityArray[moduleId];
    }
}

void Gen2ArchRuntimeConnectivity::assignCustomMapping(const ServerConnectivityUserConfig& usersConnectivityConfig)
{
    if (!usersConnectivityConfig.hasValidMapping()) return;
    // We will override all the comms with same user configuration if provided
    m_mappings = usersConnectivityConfig.getMapping();  // copy entire mapping
    LOG_HCL_INFO(HCL,
                 "m_hclCommId={}, Will be using custom mapping: {}.",
                 m_hclCommId,
                 usersConnectivityConfig.getFilePathLoaded());
}

void Gen2ArchRuntimeConnectivity::setPortsMasksGlbl()
{
    RuntimePortsMasksUtils::SetPortsMaskInput input {.comm                         = m_hclCommId,
                                                     .serverConnectivity           = m_serverConnectivity,
                                                     .operationalScaleOutPortsMask = nics_mask_t(NBITS(64))};

    RuntimePortsMasksUtils::SetPortsMaskOutput output = RuntimePortsMasksUtils::setPortsMasksCommon(input);

    m_enabledExternalPortsMaskGlbl = output.enabledExternalPortsMask;

    // Define if scaleout global context should be updated - per user request & LKD ports mask
    setUpdateScaleOutGlobalContextRequiredGlbl(m_serverConnectivity.getLkdEnabledScaleoutPorts(),
                                               nics_mask_t(output.scaleOutPortsMask));
}

int Gen2ArchRuntimeConnectivity::getRemoteDevice(const uint16_t port) const
{
    return std::get<0>(m_mappings[m_moduleId][port]);
}

uint16_t Gen2ArchRuntimeConnectivity::getPeerPort(const uint16_t port) const
{
    return std::get<1>(m_mappings[m_moduleId][port]);
}

uint16_t Gen2ArchRuntimeConnectivity::getSubPortIndex(const uint16_t port) const
{
    return std::get<2>(m_mappings[m_moduleId][port]);
}

uint16_t Gen2ArchRuntimeConnectivity::getScaleoutNicFromSubPort(const uint16_t subPort) const
{
    for (uint16_t port_idx = 0; port_idx < m_mappings[m_moduleId].size(); port_idx++)
    {
        const Gen2ArchNicDescriptor& mapping(m_mappings[m_moduleId][port_idx]);
        const uint16_t               nicInMapping     = std::get<1>(mapping);  // dest nic
        const uint16_t               subPortInMapping = std::get<2>(mapping);
        if ((nicInMapping < MAX_NICS_GEN2ARCH) && (subPortInMapping == subPort) && isScaleoutPort(port_idx))
        {
            return nicInMapping;
        }
    }

    VERIFY(false, "could not find scaleout nic for m_hclCommId={}, subPort {}", m_hclCommId, subPort);
}

bool Gen2ArchRuntimeConnectivity::isScaleoutPort(const uint16_t port) const
{
    return std::get<0>(m_mappings[m_moduleId][port]) == SCALEOUT_DEVICE_ID;
}

bool Gen2ArchRuntimeConnectivity::isPortConnected(const uint16_t port) const
{
    return std::get<0>(m_mappings[m_moduleId][port]) != NOT_CONNECTED_DEVICE_ID;
}

void Gen2ArchRuntimeConnectivity::setNumScaleUpPorts()
{
    for (uint16_t port_idx = 0; port_idx < MAX_NICS_GEN2ARCH; port_idx++)
    {
        if (isPortConnected(port_idx) && !isScaleoutPort(port_idx))
        {
            m_enabledScaleupPorts.set(port_idx);
        }
    }
}

void Gen2ArchRuntimeConnectivity::setMaxSubNics()
{
    for (uint16_t port_idx = 0; port_idx < MAX_NICS_GEN2ARCH; port_idx++)
    {
        if (isPortConnected(port_idx))
        {
            const int subPortIndex = getSubPortIndex(port_idx);
            if (!isScaleoutPort(port_idx))
            {
                if (m_maxSubNicScaleup < subPortIndex)
                {
                    m_maxSubNicScaleup = subPortIndex;
                }
            }
            else
            {
                if (m_maxSubNicScaleout < subPortIndex)
                {
                    m_maxSubNicScaleout = subPortIndex;
                }
            }
        }
    }
    LOG_HCL_DEBUG(HCL,
                  "m_hclCommId={}, m_maxSubNicScaleup={}, m_maxSubNicScaleout={}",
                  m_hclCommId,
                  m_maxSubNicScaleup,
                  m_maxSubNicScaleout);
    VERIFY(m_maxSubNicScaleup > 0);
    VERIFY(m_maxSubNicScaleout > 0);
}

uint16_t Gen2ArchRuntimeConnectivity::getMaxSubPort(const bool isScaleoutPort) const
{
    if (isScaleoutPort)
    {
        return m_maxSubNicScaleout;
    }
    else
    {
        return m_maxSubNicScaleup;
    }
}

nics_mask_t Gen2ArchRuntimeConnectivity::getAllPorts(const int         deviceId,
                                                     const nics_mask_t enabledExternalPortsMask) const
{
    nics_mask_t       ports;
    const nics_mask_t enabledPorts =
        nics_mask_t((m_serverConnectivity.getEnabledPortsMask() & ~m_fullScaleoutPorts) | enabledExternalPortsMask);

    for (unsigned port_idx = 0; port_idx < MAX_NICS_GEN2ARCH; port_idx++)
    {
        if (deviceId == getRemoteDevice(port_idx))
        {
            ports[port_idx] = enabledPorts[port_idx];
        }
    }
    return ports;
}

nics_mask_t Gen2ArchRuntimeConnectivity::getAllPortsGlbl(const int deviceId) const
{
    return getAllPorts(deviceId, m_enabledExternalPortsMaskGlbl);
}

uint64_t Gen2ArchRuntimeConnectivity::getEnabledPortsMask() const
{
    return m_serverConnectivity.getEnabledPortsMask();
}

uint16_t Gen2ArchRuntimeConnectivity::getDefaultScaleUpPort() const
{
    return m_enabledScaleupPorts(0);
}

nics_mask_t Gen2ArchRuntimeConnectivity::getScaleOutPortsGlbl() const
{
    return m_enabledScaleoutPortsGlbl;
}

nics_mask_t Gen2ArchRuntimeConnectivity::getScaleUpPorts() const
{
    return m_enabledScaleupPorts;
}

void Gen2ArchRuntimeConnectivity::verifyPortsConfiguration() const
{
    for (unsigned port_index = 0; port_index < MAX_NICS_GEN2ARCH; port_index++)
    {
        if (isScaleoutPort(port_index))
        {
            if (m_serverConnectivity.getEnabledPortsMask()[port_index] != m_enabledExternalPortsMaskGlbl[port_index])
            {
                LOG_HCL_WARN(
                    HCL,
                    "m_hclCommId={}, Inconsistency between LKD ports mask {} and ext ports mask {} for port #{}",
                    m_hclCommId,
                    m_serverConnectivity.getEnabledPortsMask().to_str(),
                    m_enabledExternalPortsMaskGlbl.to_str(),
                    port_index);
            }
        }
        else if (!m_serverConnectivity.getEnabledPortsMask()[port_index])
        {
            LOG_HCL_WARN(HCL,
                         "m_hclCommId={}, Internal port {} cannot be disabled. mask = {}",
                         m_hclCommId,
                         port_index,
                         m_serverConnectivity.getEnabledPortsMask().to_str());
        }
    }
}

void Gen2ArchRuntimeConnectivity::readAllPorts()
{
    // collect all ports that are pre-defined as scaleout ports
    for (unsigned port_index = 0; port_index < MAX_NICS_GEN2ARCH; port_index++)
    {
        if (isScaleoutPort(port_index))
        {
            m_fullScaleoutPorts[port_index] = true;
        }
        if (isPortConnected(port_index))
        {
            m_allPorts[port_index] = true;
        }
    }
    LOG_HCL_INFO(HCL,
                 "Default configuration of ports for comm {} mask is: scaleout={}, all={}",
                 m_hclCommId,
                 m_fullScaleoutPorts.to_str(),
                 m_allPorts.to_str());
}

void Gen2ArchRuntimeConnectivity::setNumScaleOutPortsGlbl()
{
    uint16_t sub_port_index_min = 0;
    uint16_t sub_port_index_max = m_serverConnectivity.getMaxNumScaleOutPorts() - 1;  // Includes LKD mask
    m_enabledScaleoutSubPortsGlbl.clear();
    m_enabledScaleoutPortsGlbl = 0;
    // collect all ports that are pre-defined as scaleout ports and enabled in hl-thunk port mask
    for (uint16_t port_index = 0; port_index < MAX_NICS_GEN2ARCH; port_index++)
    {
        if (isScaleoutPort(port_index))
        {
            // Accordingly to FW implementation, the port with the lowest sub port index
            // will be used for scaleout if some of the ports were disabled.
            // Example:
            // |         sub port indices      |    number of used ports   |         active ports        |
            // +-------------------------------+---------------------------+-----------------------------+
            // |        8->2, 22->0, 23->1     |             2             |             22,23           |
            // +-------------------------------+---------------------------+-----------------------------+
            // |        8->2, 22->0, 23->1     |             1             |               22            |
            // +-------------------------------+---------------------------+-----------------------------+
            if (m_enabledExternalPortsMaskGlbl[port_index])
            {
                m_enabledScaleoutPortsGlbl[port_index] = true;
                m_enabledScaleoutSubPortsGlbl.insert(std::make_pair(port_index, sub_port_index_min));
                sub_port_index_min++;
                LOG_HCL_INFO(HCL, "scaleout port({}) is enabled", port_index);
            }
            else
            {
                m_enabledScaleoutSubPortsGlbl.insert(std::make_pair(port_index, sub_port_index_max));
                sub_port_index_max--;
                LOG_HCL_INFO(HCL, "scaleout port({}) is disabled", port_index);
            }
        }
    }
    LOG_HCL_INFO(HCL,
                 "Enabled number of scaleout ports for comm {} by LKD/user mask is: {} out of {} possible.",
                 m_hclCommId,
                 m_enabledScaleoutPortsGlbl.to_str(),
                 m_fullScaleoutPorts.to_str());
    for (const auto kv : m_enabledScaleoutSubPortsGlbl)
    {
        LOG_HCL_DEBUG(HCL, "m_enabledScaleoutSubPortsGlbl for comm {}: [{}, {}]", m_hclCommId, kv.first, kv.second);
    }
}

uint16_t Gen2ArchRuntimeConnectivity::getNumScaleUpPorts() const
{
    return m_enabledScaleupPorts.count();
}

uint16_t Gen2ArchRuntimeConnectivity::getNumScaleOutPortsGlbl() const
{
    return m_enabledScaleoutPortsGlbl.count();
}

uint64_t Gen2ArchRuntimeConnectivity::getExternalPortsMaskGlbl() const
{
    return m_enabledExternalPortsMaskGlbl;
}

uint16_t Gen2ArchRuntimeConnectivity::getScaleoutSubPortIndexGlbl(const uint16_t port) const
{
    return m_enabledScaleoutSubPortsGlbl.at(port);
}

void Gen2ArchRuntimeConnectivity::setUpdateScaleOutGlobalContextRequiredGlbl(const nics_mask_t lkd_mask,
                                                                             const nics_mask_t scaleOutPortsMask)
{
    // If LKD enables the same ports or less than the user requested, no need to update global scaleout context
    if (lkd_mask == (lkd_mask & scaleOutPortsMask))
    {
        m_updateScaleOutGlobalContextRequiredGlbl = false;
    }
    else
    {
        m_updateScaleOutGlobalContextRequiredGlbl = true;
    }
}

namespace RuntimePortsMasksUtils
{
SetPortsMaskOutput setPortsMasksCommon(const SetPortsMaskInput input)
{
    SetPortsMaskOutput output;

    uint64_t scaleOutPortsMask = input.serverConnectivity.getUserScaleOutPortsMask();  // Physical nics mask
    LOG_HCL_DEBUG(
        HCL,
        "Started, input CommId={}, input.operationalScaleOutPortsMask={:24b}, initial scaleOutPortsMask={:24b}",
        input.comm,
        (uint64_t)input.operationalScaleOutPortsMask,
        scaleOutPortsMask);

    // m_enabled_external_ports_mask should be the minimum between
    // LKD port mask and user requested port mask and the fault-tolerance mask (input from user):
    //    (GCFG_SCALE_OUT_PORTS_MASK & GCFG_LOGICAL_SCALE_OUT_PORTS_MASK & operationalScaleOutPortsMask
    // GCFG_SCALE_OUT_PORTS_MASK.value() default = 0xc00100.
    // GCFG_LOGICAL_SCALE_OUT_PORTS_MASK.value() is logical ports mask, LSB is logical SO port 0, default is
    // 0xFFFFFF. It must be used for G3 since each device has different scaleout ports numbers Example for G2:
    // +-------------------------------+---------------------------+-----------------------------+
    // |           LKD mask            |          User mask        |          Used ports         |
    // +-------------------------------+---------------------------+-----------------------------+
    // |           0xc00100            |          0xc00000         |            22,23            |
    // |         Enabled 8,22,23       |        Enabled 22,23      |                             |
    // +-------------------------------+---------------------------+-----------------------------+
    // |           LKD mask            |      New User mask        |          Used ports         |
    // +-------------------------------+---------------------------+-----------------------------+
    // |           0xc00100            |          0xFFFFFE         |            22,23            |
    // |         Enabled 8,22,23       |        Disable 8          |                             |
    // +-------------------------------+---------------------------+-----------------------------+
    const uint16_t maxScaleoutPorts =
        input.serverConnectivity.getAllScaleoutPorts().count();
    static const nics_mask_t allScaleoutNicsBits(NBITS(maxScaleoutPorts));
    const nics_mask_t logicalScaleoutPortsMaskBits(allScaleoutNicsBits & GCFG_LOGICAL_SCALE_OUT_PORTS_MASK.value() &
                                                   input.operationalScaleOutPortsMask);
    LOG_HCL_DEBUG(HCL,
                  "maxScaleoutPorts={}, allScaleoutNicsBits={}, logicalScaleoutPortsMaskBits={}",
                  maxScaleoutPorts,
                  allScaleoutNicsBits.to_str(),
                  logicalScaleoutPortsMaskBits.to_str());
    nics_mask_t logicalScaleoutPortsMask;

    if (logicalScaleoutPortsMaskBits.count() < maxScaleoutPorts)  // any logical scaleout bit is reset?
    {
        LOG_HCL_INFO(HCL,
                     "m_hclCommId={}, User requested logical scaleout ports mask of logicalScaleoutPortsMaskBits={}",
                     input.comm,
                     logicalScaleoutPortsMaskBits.to_str());
        uint64_t scaleoutPortIndex = 0;
        for (uint16_t logical_port_idx = 0; logical_port_idx < maxScaleoutPorts; logical_port_idx++)
        {
            // find next scaleout port position from right
            while ((scaleoutPortIndex < MAX_NICS_GEN2ARCH) &&
                   !input.serverConnectivity.isScaleoutPort(scaleoutPortIndex))
            {
                scaleoutPortIndex++;
            }
            if (logicalScaleoutPortsMaskBits[logical_port_idx])  // logical bit set, set correct scaleout port bit
            {
                logicalScaleoutPortsMask.set(scaleoutPortIndex);
            }
            scaleoutPortIndex++;
        }
        LOG_HCL_DEBUG(HCL,
                      "m_hclCommId={}, Logical scaleout ports mask logicalScaleoutPortsMask={}",
                      input.comm,
                      logicalScaleoutPortsMask.to_str());
        scaleOutPortsMask &= logicalScaleoutPortsMask;
    }
    output.enabledExternalPortsMask = input.serverConnectivity.getLkdEnabledScaleoutPorts() & scaleOutPortsMask;
    output.scaleOutPortsMask        = scaleOutPortsMask;

    VERIFY(output.enabledExternalPortsMask != INVALID_PORTS_MASK, "External ports mask was not defined.");
    LOG_HCL_INFO(HCL,
                 "commId={}, initialized full ports mask {:24b} external ports mask {}, "
                 "requested "
                 "external ports mask {:024b}",
                 input.comm,
                 (uint64_t)input.serverConnectivity.getEnabledPortsMask(),
                 output.enabledExternalPortsMask.to_str(),
                 scaleOutPortsMask);

    return output;
}

}  // namespace RuntimePortsMasksUtils
