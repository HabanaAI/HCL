#include "platform/gaudi2/port_mapping.h"
#include "hcl_log_manager.h"  // for LOG_*
#include "hcl_utils.h"        // for LOG_HCL_*

Gaudi2DevicePortMapping::Gaudi2DevicePortMapping(int fd) : Gen2ArchDevicePortMapping(fd)
{
    // Keep the order of functions here
    assignDefaultMapping();
    verifyPortsConfiguration();
    setNumScaleUpPorts();
    setNumScaleOutPorts();
}

Gaudi2DevicePortMapping::Gaudi2DevicePortMapping(int fd, const Gen2ArchPortMappingConfig& portMappingConfig)
: Gen2ArchDevicePortMapping(fd)
{
    // Keep the order of functions here
    assignDefaultMapping();
    assignCustomMapping(fd, portMappingConfig);
    logPortMappingConfig(m_spotlight_mappings[DEFAULT_SPOTLIGHT]);  // DEFAULT_SPOTLIGHT is always used in G2
    verifyPortsConfiguration();
    setNumScaleUpPorts();
    setNumScaleOutPorts();
}

void Gaudi2DevicePortMapping::assignDefaultMapping()
{
    // DEFAULT_SPOTLIGHT is always used in G2
    m_spotlight_mappings[DEFAULT_SPOTLIGHT][0] = g_gaudi2_card_location_0_mapping[DEFAULT_SPOTLIGHT];
    m_spotlight_mappings[DEFAULT_SPOTLIGHT][1] = g_gaudi2_card_location_1_mapping[DEFAULT_SPOTLIGHT];
    m_spotlight_mappings[DEFAULT_SPOTLIGHT][2] = g_gaudi2_card_location_2_mapping[DEFAULT_SPOTLIGHT];
    m_spotlight_mappings[DEFAULT_SPOTLIGHT][3] = g_gaudi2_card_location_3_mapping[DEFAULT_SPOTLIGHT];
    m_spotlight_mappings[DEFAULT_SPOTLIGHT][4] = g_gaudi2_card_location_4_mapping[DEFAULT_SPOTLIGHT];
    m_spotlight_mappings[DEFAULT_SPOTLIGHT][5] = g_gaudi2_card_location_5_mapping[DEFAULT_SPOTLIGHT];
    m_spotlight_mappings[DEFAULT_SPOTLIGHT][6] = g_gaudi2_card_location_6_mapping[DEFAULT_SPOTLIGHT];
    m_spotlight_mappings[DEFAULT_SPOTLIGHT][7] = g_gaudi2_card_location_7_mapping[DEFAULT_SPOTLIGHT];
}

unsigned Gaudi2DevicePortMapping::getMaxNumScaleOutPorts() const
{
    return m_max_scaleout_ports;
}

unsigned Gaudi2DevicePortMapping::getDefaultScaleOutPortByIndex(unsigned idx) const
{
    return lkd_enabled_scaleout_ports(idx);
}

void Gaudi2DevicePortMapping::assignCustomMapping(int fd, const Gen2ArchPortMappingConfig& portMappingConfig)
{
    if (!portMappingConfig.hasValidMapping()) return;
    // DEFAULT_SPOTLIGHT is always used in G2
    m_spotlight_mappings[DEFAULT_SPOTLIGHT] = portMappingConfig.getMapping();  // copy entire mapping
    LOG_HCL_INFO(HCL, "Will be using custom mapping: {}.", portMappingConfig.getFilePathLoaded());
}