#include "platform/gaudi3/port_mapping_hls3pcie.h"

#include <cstdint>  // for uint8_t
#include <utility>  // for pair
#include <type_traits>
#include <unordered_map>
#include <ostream>  // for operator<<, ostream
#include <unordered_set>

#include "hcl_log_manager.h"                                // log_*
#include "platform/gen2_arch_common/port_mapping_config.h"  // for Gen2ArchPortMappingConfig
#include "platform/gen2_arch_common/port_mapping.h"         // for ServerNicsConnectivityArray
#include "platform/gaudi3/port_mapping_autogen_hls3pcie.h"  // for g_hls3pcie_card_location*
#include "platform/gaudi3/hal.h"                            // for Gaudi3Hal

static const ServerNicsConnectivityArray s_hls3PcieNicsConnectivityArray = {g_hls3pcie_card_location_0_mapping,
                                                                            g_hls3pcie_card_location_1_mapping,
                                                                            g_hls3pcie_card_location_2_mapping,
                                                                            g_hls3pcie_card_location_3_mapping,
                                                                            g_hls3pcie_card_location_4_mapping,
                                                                            g_hls3pcie_card_location_5_mapping,
                                                                            g_hls3pcie_card_location_6_mapping,
                                                                            g_hls3pcie_card_location_7_mapping};

HLS3PciePortMapping::HLS3PciePortMapping(const int                        fd,
                                         const Gen2ArchPortMappingConfig& portMappingConfig,
                                         const hcl::Gaudi3Hal&            hal)
: Gaudi3DevicePortMapping(fd, portMappingConfig, hal, s_hls3PcieNicsConnectivityArray)
{
    LOG_HCL_DEBUG(HCL, "ctor called");
}

HLS3PciePortMapping::HLS3PciePortMapping(const int fd, const hcl::Gaudi3Hal& hal)
: Gaudi3DevicePortMapping(fd, hal, s_hls3PcieNicsConnectivityArray)
{
    LOG_HCL_DEBUG(HCL, "test ctor 1 called");
}

HLS3PciePortMapping::HLS3PciePortMapping(const int fd, const int moduleId, const hcl::Gaudi3Hal& hal)
: Gaudi3DevicePortMapping(fd, moduleId, hal, s_hls3PcieNicsConnectivityArray)
{
    LOG_HCL_DEBUG(HCL, "test ctor 2 called");
}
