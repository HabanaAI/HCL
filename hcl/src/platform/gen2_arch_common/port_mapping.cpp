#include "platform/gen2_arch_common/port_mapping.h"

#include <cstddef>                  // for size_t
#include <cstdint>                  // for uint8_t
#include <memory>                   // for allocator_traits<>::value_type
#include "hcl_utils.h"              // for VERIFY
#include "hlthunk.h"                // for hlthunk_get_hw_ip_info, hlth...
#include "platform/gen2_arch_common/types.h"  // for HCL_INVALID_PORT
#include "hcl_log_manager.h"        // for LOG_*

static constexpr unsigned INVALID_PORTS_MASK = (unsigned)-1;

Gen2ArchDevicePortMapping::Gen2ArchDevicePortMapping(int fd) : m_fd(fd)
{
    if (m_fd >= 0)
    {
        struct hlthunk_hw_ip_info              hw_ip;
        struct hlthunk_nic_get_ports_masks_out ports_masks;
        hlthunk_get_hw_ip_info(m_fd, &hw_ip);

        // DEFAULT_SPOTLIGHT can be used since we compare the size only, which is always the same
        VERIFY(hw_ip.module_id < Gen2ArchDevicePortMapping::m_spotlight_mappings[DEFAULT_SPOTLIGHT].size(),
               "Unexpected module id");

        m_moduleId = hw_ip.module_id;

        // Get port mask from LKD
        int ret = hlthunk_nic_get_ports_masks(m_fd, &ports_masks);
        if (ret)
        {
            LOG_ERR(HCL, "Could not read port mask from hl-thunk: {}", ret);
            m_enabled_ports_mask          = INVALID_PORTS_MASK;
            m_enabled_external_ports_mask = INVALID_PORTS_MASK;
        }
        else
        {
            // m_enabled_external_ports_mask should be the minimum between
            // LKD port mask and user requested port mask.
            // GCFG_SCALE_OUT_PORTS_MASK.value() default = 0xc00100.
            // Example:
            // +-------------------------------+---------------------------+-----------------------------+
            // |           LKD mask            |          User mask        |          Used ports         |
            // +-------------------------------+---------------------------+-----------------------------+
            // |           0xc00100            |          0xc00000         |            22,23            |
            // |         Enabled 8,22,23       |        Enabled 22,23      |                             |
            // +-------------------------------+---------------------------+-----------------------------+
            m_enabled_ports_mask          = ports_masks.ports_mask;
            m_enabled_external_ports_mask = ports_masks.ext_ports_mask & GCFG_SCALE_OUT_PORTS_MASK.value();

            // Set max number of scaleout ports accordingly to LKD mask (not per user requests)
            setMaxNumScaleOutPorts(ports_masks.ext_ports_mask);

            // Define if scaleout global context should be updated - per user request & LKD ports mask
            setUpateScaleOutGlobalContextRequired(ports_masks.ext_ports_mask);
        }

        VERIFY(m_enabled_ports_mask != INVALID_PORTS_MASK, "Internal ports mask was not defined.");
        VERIFY(m_enabled_external_ports_mask != INVALID_PORTS_MASK, "External ports mask was not defined.");
        LOG_DEBUG(
            HCL,
            "PortMapping initialized with module_id {}, ports mask {} external ports mask {}, user requested "
            "external ports mask {:024b}",
            m_moduleId,
            m_enabled_ports_mask.to_str(),
            m_enabled_external_ports_mask.to_str(),
            GCFG_SCALE_OUT_PORTS_MASK.value());
    }
    else
    {
        m_enabled_ports_mask = INVALID_PORTS_MASK;
    }
}

Gen2ArchDevicePortMapping::Gen2ArchDevicePortMapping(const int fd, const int moduleId) : m_moduleId(moduleId), m_fd(fd)
{
    LOG_HCL_DEBUG(HCL, "unit test device ctor");
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

bool Gen2ArchDevicePortMapping::isScaleoutPort(const unsigned port, const unsigned spotlightType) const
{
    return std::get<0>(m_spotlight_mappings[spotlightType][m_moduleId][port]) == SCALEOUT_DEVICE_ID;
}

void Gen2ArchDevicePortMapping::setNumScaleUpPorts()
{
    for (unsigned spotlight_type = 0; spotlight_type < MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH; spotlight_type++)
    {
        for (unsigned port_idx = 0; port_idx < MAX_NICS_GEN2ARCH; port_idx++)
        {
            if (!isScaleoutPort(port_idx, spotlight_type))
            {
                m_enabled_scaleup_ports[spotlight_type].set(port_idx);
            }
        }
    }
}

int Gen2ArchDevicePortMapping::getMaxSubPort(unsigned spotlightType) const
{
    static int maxSubPort = -1;
    if (unlikely(maxSubPort == -1))
    {
        for (size_t i = 0; i < MAX_NICS_GEN2ARCH; i++)
        {
            int subPortIndex = getSubPortIndex(i, spotlightType);
            if (maxSubPort < subPortIndex)
            {
                maxSubPort = getSubPortIndex(i, spotlightType);
            }
        }
    }
    return maxSubPort;
}

nics_mask_t Gen2ArchDevicePortMapping::getAllPorts(int deviceId, unsigned spotlightType) const
{
    nics_mask_t ports;
    for (unsigned port_idx = 0; port_idx < MAX_NICS_GEN2ARCH; port_idx++)
    {
        if (deviceId == getRemoteDevice(port_idx, spotlightType))
        {
            ports[port_idx] = m_enabled_ports_mask[port_idx] || m_enabled_external_ports_mask[port_idx];
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
    for (int port_index = 0; port_index < MAX_NICS_GEN2ARCH; port_index++)
    {
        if (isScaleoutPort(port_index, spotlightType))
        {
            if (m_enabled_ports_mask[port_index] != m_enabled_external_ports_mask[port_index])
            {
                LOG_WARN(HCL,
                         "inconsistency between ports mask {} and ext ports mask {} for port #{}",
                         m_enabled_ports_mask.to_str(),
                         m_enabled_external_ports_mask.to_str(),
                         port_index);
            }
        }
        else if (!m_enabled_ports_mask[port_index])
        {
            LOG_WARN(HCL, "internal port {} cannot be disabled. mask = {}",
                     port_index,
                     m_enabled_ports_mask.to_str());
        }
    }
}

void Gen2ArchDevicePortMapping::setNumScaleOutPorts()
{
    unsigned sub_port_index_min = 0;
    unsigned sub_port_index_max = getMaxNumScaleOutPorts() - 1;

    for (int spotlight_type = 0; spotlight_type < MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH; spotlight_type++)
    {
        // collect all ports that are pre-defined as scaleout ports and enabled in hl-thunk port mask
        for (unsigned port_index = 0; port_index < MAX_NICS_GEN2ARCH; port_index++)
        {
            if (isScaleoutPort(port_index, spotlight_type))
            {
                // accordingly to FW implementation, the port with the lowest sub port index
                // will be used for scaleout if some of the ports were disabled.
                // Example:
                // |         sub port indices      |    number of used ports   |         active ports        |
                // +-------------------------------+---------------------------+-----------------------------+
                // |        8->3, 22->0, 23->1     |             2             |             22,23           |
                // +-------------------------------+---------------------------+-----------------------------+
                // |        8->3, 22->0, 23->1     |             1             |               22            |
                // +-------------------------------+---------------------------+-----------------------------+
                if (m_enabled_external_ports_mask[port_index])
                {
                    m_enabled_scaleout_ports[spotlight_type][port_index]=true;
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
        LOG_INFO(HCL,
                 "Enabled number of scalout ports for spotlight type {} by hlthunk mask is: {}.",
                 spotlight_type,
                 m_enabled_scaleout_ports[spotlight_type].count());
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

void Gen2ArchDevicePortMapping::setUpateScaleOutGlobalContextRequired(uint64_t lkd_mask)
{
    // If LKD enables the same ports or less than the user requested, no need to update global scaleout context
    if (lkd_mask == (lkd_mask & GCFG_SCALE_OUT_PORTS_MASK.value()))
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

void Gen2ArchDevicePortMapping::setMaxNumScaleOutPorts(uint64_t lkd_mask)
{
    lkd_enabled_scaleout_ports = lkd_mask;
    m_max_scaleout_ports       = lkd_enabled_scaleout_ports.count();
}
