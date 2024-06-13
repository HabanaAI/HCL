#pragma once

#include <cstdint>        // for uint8_t
#include <array>          // for array
#include <map>            // for map
#include <utility>        // for pair
#include <vector>         // for vector
#include <unordered_set>  // for unordered_set

#include "platform/gaudi3/port_mapping.h"                   // for Gaudi3DevicePortMapping
#include "platform/gen2_arch_common/port_mapping_config.h"  // for Gen2ArchPortMappingConfig
#include "platform/gaudi3/hal.h"                            // for Gaudi3Hal

class HLS3PciePortMapping : public Gaudi3DevicePortMapping
{
public:
    HLS3PciePortMapping(const int fd, const hcl::Gaudi3Hal& hal);                      // for testing
    HLS3PciePortMapping(const int fd, const int moduleId, const hcl::Gaudi3Hal& hal);  // for testing
    HLS3PciePortMapping(const int fd, const Gen2ArchPortMappingConfig& portMappingConfig, const hcl::Gaudi3Hal& hal);
    virtual ~HLS3PciePortMapping() = default;
};
