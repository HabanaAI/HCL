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
    setPortsMasks();
    verifyPortsConfiguration();
    setNumScaleUpPorts();
    setNumScaleOutPorts();
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

void Gen2ArchRuntimeConnectivity::setPortsMasks()
{
    uint64_t scaleOutPortsMask = m_serverConnectivity.getUserScaleOutPortsMask();
    LOG_HCL_DEBUG(HCL, "Started, m_hclCommId={}, scaleOutPortsMask={:024b}", m_hclCommId, scaleOutPortsMask);

    // m_enabled_external_ports_mask should be the minimum between
    // LKD port mask and user requested port mask (GCFG_SCALE_OUT_PORTS_MASK & GCFG_LOGICAL_SCALE_OUT_PORTS_MASK)
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
    const uint16_t           maxScaleoutPorts = m_fullScaleoutPorts.count();  // TODO: collect for all comms
    static const nics_mask_t allScaleoutNicsBits(NBITS(maxScaleoutPorts));
    const nics_mask_t logicalScaleoutPortsMaskBits(allScaleoutNicsBits & GCFG_LOGICAL_SCALE_OUT_PORTS_MASK.value());
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
                     m_hclCommId,
                     logicalScaleoutPortsMaskBits.to_str());
        uint64_t scaleoutPortIndex = 0;
        for (uint16_t logical_port_idx = 0; logical_port_idx < maxScaleoutPorts; logical_port_idx++)
        {
            // find next scaleout port position from right
            while ((scaleoutPortIndex < MAX_NICS_GEN2ARCH) && !isScaleoutPort(scaleoutPortIndex))
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
                      m_hclCommId,
                      logicalScaleoutPortsMask.to_str());
        scaleOutPortsMask &= logicalScaleoutPortsMask;
    }
    m_enabled_external_ports_mask = m_serverConnectivity.getLkdEnabledScaleoutPorts() & scaleOutPortsMask;

    // Define if scaleout global context should be updated - per user request & LKD ports mask
    setUpdateScaleOutGlobalContextRequired(m_serverConnectivity.getLkdEnabledScaleoutPorts(), scaleOutPortsMask);

    VERIFY(m_enabled_external_ports_mask != INVALID_PORTS_MASK, "External ports mask was not defined.");
    LOG_HCL_INFO(HCL,
                 "m_hclCommId={}, initialized with module_id {}, full ports mask {} external ports mask {}, "
                 "user requested "
                 "external ports mask {:024b}",
                 m_hclCommId,
                 m_moduleId,
                 m_serverConnectivity.getEnabledPortsMask().to_str(),
                 m_enabled_external_ports_mask.to_str(),
                 scaleOutPortsMask);
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
            m_enabled_scaleup_ports.set(port_idx);
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

nics_mask_t Gen2ArchRuntimeConnectivity::getAllPorts(const int deviceId) const
{
    nics_mask_t       ports;
    const nics_mask_t enabledPorts =
        (m_serverConnectivity.getEnabledPortsMask() & ~m_fullScaleoutPorts) | m_enabled_external_ports_mask;
    for (unsigned port_idx = 0; port_idx < MAX_NICS_GEN2ARCH; port_idx++)
    {
        if (deviceId == getRemoteDevice(port_idx))
        {
            ports[port_idx] = enabledPorts[port_idx];
        }
    }
    return ports;
}

uint64_t Gen2ArchRuntimeConnectivity::getEnabledPortsMask() const
{
    return m_serverConnectivity.getEnabledPortsMask();
}

uint16_t Gen2ArchRuntimeConnectivity::getDefaultScaleUpPort() const
{
    return m_enabled_scaleup_ports(0);
}

nics_mask_t Gen2ArchRuntimeConnectivity::getScaleOutPorts() const
{
    return m_enabled_scaleout_ports;
}

nics_mask_t Gen2ArchRuntimeConnectivity::getScaleUpPorts() const
{
    return m_enabled_scaleup_ports;
}

void Gen2ArchRuntimeConnectivity::verifyPortsConfiguration() const
{
    for (unsigned port_index = 0; port_index < MAX_NICS_GEN2ARCH; port_index++)
    {
        if (isScaleoutPort(port_index))
        {
            if (m_serverConnectivity.getEnabledPortsMask()[port_index] != m_enabled_external_ports_mask[port_index])
            {
                LOG_HCL_WARN(
                    HCL,
                    "m_hclCommId={}, Inconsistency between LKD ports mask {} and ext ports mask {} for port #{}",
                    m_hclCommId,
                    m_serverConnectivity.getEnabledPortsMask().to_str(),
                    m_enabled_external_ports_mask.to_str(),
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

void Gen2ArchRuntimeConnectivity::setNumScaleOutPorts()
{
    uint16_t sub_port_index_min = 0;
    uint16_t sub_port_index_max = m_serverConnectivity.getMaxNumScaleOutPorts() - 1;  // Includes LKD mask

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
            if (m_enabled_external_ports_mask[port_index])
            {
                m_enabled_scaleout_ports[port_index] = true;
                m_enabled_scaleout_sub_ports.insert(std::make_pair(port_index, sub_port_index_min));
                sub_port_index_min++;
            }
            else
            {
                m_enabled_scaleout_sub_ports.insert(std::make_pair(port_index, sub_port_index_max));
                sub_port_index_max--;
            }
        }
    }
    LOG_HCL_INFO(HCL,
                 "Enabled number of scaleout ports for comm {} by LKD/user mask is: {} out of {} possible.",
                 m_hclCommId,
                 m_enabled_scaleout_ports.to_str(),
                 m_fullScaleoutPorts.to_str());
    for (const auto kv : m_enabled_scaleout_sub_ports)
    {
        LOG_HCL_DEBUG(HCL, "m_enabled_scaleout_sub_ports for comm {}: [{}, {}]", m_hclCommId, kv.first, kv.second);
    }
}

uint16_t Gen2ArchRuntimeConnectivity::getNumScaleUpPorts() const
{
    return m_enabled_scaleup_ports.count();
}

uint16_t Gen2ArchRuntimeConnectivity::getNumScaleOutPorts() const
{
    return m_enabled_scaleout_ports.count();
}

uint64_t Gen2ArchRuntimeConnectivity::getExternalPortsMask() const
{
    return m_enabled_external_ports_mask;
}

uint16_t Gen2ArchRuntimeConnectivity::getScaleoutSubPortIndex(const uint16_t port) const
{
    return m_enabled_scaleout_sub_ports.at(port);
}

void Gen2ArchRuntimeConnectivity::setUpdateScaleOutGlobalContextRequired(const uint64_t lkd_mask,
                                                                         const uint64_t scaleOutPortsMask)
{
    // If LKD enables the same ports or less than the user requested, no need to update global scaleout context
    if (lkd_mask == (lkd_mask & scaleOutPortsMask))
    {
        m_updateScaleOutGlobalContextRequired = false;
    }
    else
    {
        m_updateScaleOutGlobalContextRequired = true;
    }
}
