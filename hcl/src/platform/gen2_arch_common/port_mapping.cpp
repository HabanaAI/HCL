#include "platform/gen2_arch_common/port_mapping.h"

#include <cstddef>                  // for size_t
#include <cstdint>                  // for uint8_t
#include <memory>                   // for allocator_traits<>::value_type

#include "synapse_common_types.h"  // for synDeviceType
#include "hcl_utils.h"              // for VERIFY
#include "hlthunk.h"                // for hlthunk_get_hw_ip_info, hlth...
#include "platform/gen2_arch_common/types.h"  // for HCL_INVALID_PORT, MAX_NICS_GEN2ARCH
#include "hcl_log_manager.h"        // for LOG_*

static constexpr unsigned INVALID_PORTS_MASK = (unsigned)-1;

Gen2ArchDevicePortMapping::Gen2ArchDevicePortMapping(int fd) : m_fd(fd)
{
    m_enabled_ports_mask          = INVALID_PORTS_MASK;
    m_enabled_external_ports_mask = INVALID_PORTS_MASK;
    if (m_fd >= 0)
    {
        struct hlthunk_hw_ip_info hw_ip;
        hlthunk_get_hw_ip_info(m_fd, &hw_ip);

        // DEFAULT_SPOTLIGHT can be used since we compare the size only, which is always the same
        VERIFY(hw_ip.module_id < Gen2ArchDevicePortMapping::m_spotlight_mappings[DEFAULT_SPOTLIGHT].size(),
               "Unexpected module id");

        m_moduleId = hw_ip.module_id;
    }
}

Gen2ArchDevicePortMapping::Gen2ArchDevicePortMapping(const int fd, const int moduleId) : m_moduleId(moduleId), m_fd(fd)
{
    LOG_HCL_DEBUG(HCL, "unit test device ctor");
}

void Gen2ArchDevicePortMapping::setPortsMasks()
{
    uint64_t scaleOutPortsMask = GCFG_SCALE_OUT_PORTS_MASK.value();
    LOG_HCL_DEBUG(HCL, "Started, scaleOutPortsMask={:024b}", scaleOutPortsMask);

    struct hlthunk_nic_get_ports_masks_out ports_masks;
    // Get port mask from LKD
    const int ret = hlthunk_nic_get_ports_masks(m_fd, &ports_masks);
    if (ret)
    {
        LOG_HCL_ERR(HCL, "Could not read port mask from hl-thunk: {}", ret);
    }
    else
    {
        LOG_HCL_DEBUG(HCL,
                      "LKD: ports_mask={:024b}, ext_ports_mask={:024b}",
                      ports_masks.ports_mask,
                      ports_masks.ext_ports_mask);
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
        const unsigned int maxScaleoutPorts =
            m_fullScaleoutPorts[DEFAULT_SPOTLIGHT].count();  // TODO: collect for all spotlights ?
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
                         "User requested logical scaleout ports mask of logicalScaleoutPortsMaskBits={}",
                         logicalScaleoutPortsMaskBits.to_str());
            uint64_t scaleoutPortIndex = 0;
            for (unsigned logical_port_idx = 0; logical_port_idx < maxScaleoutPorts; logical_port_idx++)
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
                          "Logical scaleout ports mask logicalScaleoutPortsMask={}",
                          logicalScaleoutPortsMask.to_str());
            scaleOutPortsMask &= logicalScaleoutPortsMask;
        }
        m_enabled_ports_mask          = ports_masks.ports_mask;
        m_enabled_external_ports_mask = ports_masks.ext_ports_mask & scaleOutPortsMask;

        // Set max number of scaleout ports accordingly to LKD mask (not per user requests)
        setMaxNumScaleOutPorts(ports_masks.ext_ports_mask);

        // Define if scaleout global context should be updated - per user request & LKD ports mask
        setUpateScaleOutGlobalContextRequired(ports_masks.ext_ports_mask, scaleOutPortsMask);
    }

    VERIFY(m_enabled_ports_mask != INVALID_PORTS_MASK, "Internal ports mask was not defined.");
    VERIFY(m_enabled_external_ports_mask != INVALID_PORTS_MASK, "External ports mask was not defined.");
    LOG_HCL_DEBUG(HCL,
                  "PortMapping initialized with module_id {}, ports mask {} external ports mask {}, user requested "
                  "external ports mask {:024b}",
                  m_moduleId,
                  m_enabled_ports_mask.to_str(),
                  m_enabled_external_ports_mask.to_str(),
                  scaleOutPortsMask);
}

int Gen2ArchDevicePortMapping::getRemoteDevice(int port, unsigned spotlightType) const
{
    return std::get<0>(m_spotlight_mappings[spotlightType][m_moduleId][port]);
}

int Gen2ArchDevicePortMapping::getPeerPort(int port, unsigned spotlightType) const
{
    return std::get<1>(m_spotlight_mappings[spotlightType][m_moduleId][port]);
}

int Gen2ArchDevicePortMapping::getSubPortIndex(int port, unsigned spotlightType) const
{
    return std::get<2>(m_spotlight_mappings[spotlightType][m_moduleId][port]);
}

int Gen2ArchDevicePortMapping::getScaleoutNicFromSubPort(const int subPort, const unsigned spotlightType) const
{
    static constexpr int INVALID_NIC = -1;
    for (auto& mapping : m_spotlight_mappings[spotlightType][m_moduleId])
    {
        int subPortInMapping = std::get<2>(mapping);
        int nicInMapping     = std::get<1>(mapping);

        if (nicInMapping == INVALID_NIC) continue;

        if (subPortInMapping == subPort && isScaleoutPort(nicInMapping))
        {
            return nicInMapping;
        }
    }

    VERIFY(false, "could not find scaleout nic for subPort {}", subPort);
}

bool Gen2ArchDevicePortMapping::isScaleoutPort(const unsigned port, const unsigned spotlightType) const
{
    return std::get<0>(m_spotlight_mappings[spotlightType][m_moduleId][port]) == SCALEOUT_DEVICE_ID;
}

bool Gen2ArchDevicePortMapping::isPortConnected(const uint16_t port, const unsigned spotlightType) const
{
    return std::get<0>(m_spotlight_mappings[spotlightType][m_moduleId][port]) != NOT_CONNECTED_DEVICE_ID;
}

void Gen2ArchDevicePortMapping::setNumScaleUpPorts()
{
    for (unsigned spotlight_type = 0; spotlight_type < MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH; spotlight_type++)
    {
        for (unsigned port_idx = 0; port_idx < MAX_NICS_GEN2ARCH; port_idx++)
        {
            if (isPortConnected(port_idx, spotlight_type) && !isScaleoutPort(port_idx, spotlight_type))
            {
                m_enabled_scaleup_ports[spotlight_type].set(port_idx);
            }
        }
    }
}

void Gen2ArchDevicePortMapping::setMaxSubNics()
{
    for (unsigned spotlight_type = 0; spotlight_type < MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH; spotlight_type++)
    {
        for (unsigned port_idx = 0; port_idx < MAX_NICS_GEN2ARCH; port_idx++)
        {
            if (isPortConnected(port_idx, spotlight_type))
            {
                const int subPortIndex = getSubPortIndex(port_idx, spotlight_type);
                if (!isScaleoutPort(port_idx, spotlight_type))
                {
                    if (m_maxSubNicScaleup[spotlight_type] < subPortIndex)
                    {
                        m_maxSubNicScaleup[spotlight_type] = subPortIndex;
                    }
                }
                else
                {
                    if (m_maxSubNicScaleout[spotlight_type] < subPortIndex)
                    {
                        m_maxSubNicScaleout[spotlight_type] = subPortIndex;
                    }
                }
            }
        }
        LOG_HCL_DEBUG(HCL,
                      "m_maxSubNicScaleup[{}]={}, m_maxSubNicScaleout[{}]={}",
                      spotlight_type,
                      m_maxSubNicScaleup[spotlight_type],
                      spotlight_type,
                      m_maxSubNicScaleout[spotlight_type]);
        VERIFY(m_maxSubNicScaleup[spotlight_type] > 0);
        VERIFY(m_maxSubNicScaleout[spotlight_type] > 0);
    }
}

int Gen2ArchDevicePortMapping::getMaxSubPort(const bool isScaleoutPort, const unsigned spotlightType) const
{
    if (isScaleoutPort)
    {
        return m_maxSubNicScaleout[spotlightType];
    }
    else
    {
        return m_maxSubNicScaleup[spotlightType];
    }
}

nics_mask_t Gen2ArchDevicePortMapping::getAllPorts(int deviceId, unsigned spotlightType) const
{
    nics_mask_t ports;
    const nics_mask_t enabledPorts =
        (m_enabled_ports_mask & ~m_fullScaleoutPorts[spotlightType]) | m_enabled_external_ports_mask;
    for (unsigned port_idx = 0; port_idx < MAX_NICS_GEN2ARCH; port_idx++)
    {
        if (deviceId == getRemoteDevice(port_idx, spotlightType))
        {
            ports[port_idx] = enabledPorts[port_idx];
        }
    }
    return ports;
}

uint64_t Gen2ArchDevicePortMapping::getEnabledPortsMask() const
{
    return m_enabled_ports_mask;
}

nics_mask_t Gen2ArchDevicePortMapping::getScaleOutPorts(unsigned spotlightType) const
{
    return m_enabled_scaleout_ports[spotlightType];
}

void Gen2ArchDevicePortMapping::verifyPortsConfiguration(unsigned spotlightType) const
{
    for (unsigned port_index = 0; port_index < MAX_NICS_GEN2ARCH; port_index++)
    {
        if (isScaleoutPort(port_index, spotlightType))
        {
            if (m_enabled_ports_mask[port_index] != m_enabled_external_ports_mask[port_index])
            {
                LOG_HCL_WARN(HCL,
                             "inconsistency between LKD ports mask {} and ext ports mask {} for port #{}",
                             m_enabled_ports_mask.to_str(),
                             m_enabled_external_ports_mask.to_str(),
                             port_index);
            }
        }
        else if (!m_enabled_ports_mask[port_index])
        {
            LOG_HCL_WARN(HCL,
                         "internal port {} cannot be disabled. mask = {}",
                         port_index,
                         m_enabled_ports_mask.to_str());
        }
    }
}

void Gen2ArchDevicePortMapping::readMaxScaleOutPorts()
{
    for (unsigned int spotlight_type = 0; spotlight_type < MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH; spotlight_type++)
    {
        // collect all ports that are pre-defined as scaleout ports
        for (unsigned port_index = 0; port_index < MAX_NICS_GEN2ARCH; port_index++)
        {
            if (isScaleoutPort(port_index, spotlight_type))
            {
                m_fullScaleoutPorts[spotlight_type][port_index] = true;
            }
        }
        LOG_HCL_INFO(HCL,
                     "Configuration of scalout ports for spotlight type {} mask is: {}",
                     spotlight_type,
                     m_fullScaleoutPorts[spotlight_type].to_str());
    }
}

void Gen2ArchDevicePortMapping::setNumScaleOutPorts()
{
    for (unsigned spotlight_type = 0; spotlight_type < MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH; spotlight_type++)
    {
        unsigned sub_port_index_min = 0;
        unsigned sub_port_index_max = getMaxNumScaleOutPorts() - 1;  // Includes LKD mask

        // collect all ports that are pre-defined as scaleout ports and enabled in hl-thunk port mask
        for (unsigned port_index = 0; port_index < MAX_NICS_GEN2ARCH; port_index++)
        {
            if (isScaleoutPort(port_index, spotlight_type))
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
                    m_enabled_scaleout_ports[spotlight_type][port_index] = true;
                    m_enabled_scaleout_sub_ports[spotlight_type].insert(std::make_pair(port_index, sub_port_index_min));
                    sub_port_index_min++;
                }
                else
                {
                    m_enabled_scaleout_sub_ports[spotlight_type].insert(std::make_pair(port_index, sub_port_index_max));
                    sub_port_index_max--;
                }
            }
        }
        LOG_HCL_INFO(
            HCL,
            "Enabled number of scaleout ports for spotlight type {} by LKD/user mask is: {} out of {} possible.",
            spotlight_type,
            m_enabled_scaleout_ports[spotlight_type].to_str(),
            m_fullScaleoutPorts[spotlight_type].to_str());
        for (const auto kv : m_enabled_scaleout_sub_ports[spotlight_type])
        {
            LOG_HCL_DEBUG(HCL,
                          "m_enabled_scaleout_sub_ports for spotlight type {}: [{}, {}]",
                          spotlight_type,
                          kv.first,
                          kv.second);
        }
    }
}

unsigned Gen2ArchDevicePortMapping::getNumScaleUpPorts(const unsigned spotlightType) const
{
    return m_enabled_scaleup_ports[spotlightType].count();
}

unsigned Gen2ArchDevicePortMapping::getNumScaleOutPorts(unsigned spotlightType) const
{
    return m_enabled_scaleout_ports[spotlightType].count();
}

uint64_t Gen2ArchDevicePortMapping::getExternalPortsMask() const
{
    return m_enabled_external_ports_mask;
}

unsigned Gen2ArchDevicePortMapping::getScaleoutSubPortIndex(unsigned port, unsigned spotlightType)
{
    return m_enabled_scaleout_sub_ports[spotlightType][port];
}

void Gen2ArchDevicePortMapping::setUpateScaleOutGlobalContextRequired(const uint64_t lkd_mask,
                                                                      const uint64_t scaleOutPortsMask)
{
    // If LKD enables the same ports or less than the user requested, no need to update global scaleout context
    if (lkd_mask == (lkd_mask & scaleOutPortsMask))
    {
        m_upateScaleOutGlobalContextRequired = 0;
    }
    else
    {
        m_upateScaleOutGlobalContextRequired = 1;
    }
}

bool Gen2ArchDevicePortMapping::isUpateScaleOutGlobalContextRequired()
{
    return m_upateScaleOutGlobalContextRequired;
}

void Gen2ArchDevicePortMapping::setMaxNumScaleOutPorts(const uint64_t lkd_mask)
{
    m_lkd_enabled_scaleout_ports = lkd_mask;
    m_max_scaleout_ports         = m_lkd_enabled_scaleout_ports.count();
}
