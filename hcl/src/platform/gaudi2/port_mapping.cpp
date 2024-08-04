#include "platform/gaudi2/port_mapping.h"
#include "hcl_log_manager.h"  // for LOG_*
#include "hcl_utils.h"        // for LOG_HCL_*

Gaudi2DevicePortMapping::Gaudi2DevicePortMapping(int fd) : Gen2ArchDevicePortMapping(fd)
{
    // unit tests ctor base class
    // internal ctor functions should be called from subclass of unit tests ctor
}

Gaudi2DevicePortMapping::Gaudi2DevicePortMapping(int fd, const Gen2ArchPortMappingConfig& portMappingConfig)
: Gen2ArchDevicePortMapping(fd)
{
    // Keep the order of functions here
    assignDefaultMapping();
    assignCustomMapping(portMappingConfig);
    logPortMappingConfig(m_spotlight_mappings[DEFAULT_SPOTLIGHT]);  // DEFAULT_SPOTLIGHT is always used in G2
    readMaxScaleOutPorts();
    setPortsMasks();
    verifyPortsConfiguration();
    setNumScaleUpPorts();
    setNumScaleOutPorts();
    setMaxSubNics();
}

void Gaudi2DevicePortMapping::assignDefaultMapping()
{
    // DEFAULT_SPOTLIGHT is always used in G2, but we fill for MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH
    for (unsigned i = 0; i < MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH; i++)
    {
        m_spotlight_mappings[i][0] = g_gaudi2_card_location_0_mapping[DEFAULT_SPOTLIGHT];
        m_spotlight_mappings[i][1] = g_gaudi2_card_location_1_mapping[DEFAULT_SPOTLIGHT];
        m_spotlight_mappings[i][2] = g_gaudi2_card_location_2_mapping[DEFAULT_SPOTLIGHT];
        m_spotlight_mappings[i][3] = g_gaudi2_card_location_3_mapping[DEFAULT_SPOTLIGHT];
        m_spotlight_mappings[i][4] = g_gaudi2_card_location_4_mapping[DEFAULT_SPOTLIGHT];
        m_spotlight_mappings[i][5] = g_gaudi2_card_location_5_mapping[DEFAULT_SPOTLIGHT];
        m_spotlight_mappings[i][6] = g_gaudi2_card_location_6_mapping[DEFAULT_SPOTLIGHT];
        m_spotlight_mappings[i][7] = g_gaudi2_card_location_7_mapping[DEFAULT_SPOTLIGHT];
    }
}

unsigned Gaudi2DevicePortMapping::getDefaultScaleOutPortByIndex(unsigned idx) const
{
    return m_lkd_enabled_scaleout_ports(idx);
}

void Gaudi2DevicePortMapping::assignCustomMapping(const Gen2ArchPortMappingConfig& portMappingConfig)
{
    if (!portMappingConfig.hasValidMapping()) return;
    // DEFAULT_SPOTLIGHT is always used in G2
    m_spotlight_mappings[DEFAULT_SPOTLIGHT] = portMappingConfig.getMapping();  // copy entire mapping
    LOG_HCL_INFO(HCL, "Will be using custom mapping: {}.", portMappingConfig.getFilePathLoaded());
}