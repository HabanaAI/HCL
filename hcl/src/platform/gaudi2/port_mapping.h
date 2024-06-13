#pragma once

#include <cstdint>                                  // for uint8_t
#include <array>                                    // for array
#include <map>                                      // for map
#include <utility>                                  // for pair
#include <vector>                                   // for vector

#include "platform/gen2_arch_common/port_mapping.h" // for Gen2ArchNicDescr...
#include "platform/gen2_arch_common/types.h"        // for MAX_NICS_GEN2ARCH
#include "platform/gen2_arch_common/port_mapping_config.h"  // for Gen2ArchNicDescriptor, Gen2ArchPortMappingConfig

extern Gen2ArchNicsDeviceConfig g_gaudi2_card_location_0_mapping;
extern Gen2ArchNicsDeviceConfig g_gaudi2_card_location_1_mapping;
extern Gen2ArchNicsDeviceConfig g_gaudi2_card_location_2_mapping;
extern Gen2ArchNicsDeviceConfig g_gaudi2_card_location_3_mapping;
extern Gen2ArchNicsDeviceConfig g_gaudi2_card_location_4_mapping;
extern Gen2ArchNicsDeviceConfig g_gaudi2_card_location_5_mapping;
extern Gen2ArchNicsDeviceConfig g_gaudi2_card_location_6_mapping;
extern Gen2ArchNicsDeviceConfig g_gaudi2_card_location_7_mapping;

class Gaudi2DevicePortMapping : public Gen2ArchDevicePortMapping
{
public:
    Gaudi2DevicePortMapping(int fd);
    Gaudi2DevicePortMapping(int fd, const Gen2ArchPortMappingConfig& portMappingConfig);
    virtual void assignDefaultMapping() override;
    unsigned     getMaxNumScaleOutPorts() const override;
    unsigned     getDefaultScaleOutPortByIndex(unsigned idx) const override;
    virtual void assignCustomMapping(int fd, const Gen2ArchPortMappingConfig& portMappingConfig) override;
};
