#include "platform/gaudi3/hls3rack_server_connectivity.h"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint*_t

#include "hcl_api_types.h"                                   // for HCL_Comm
#include "platform/gen2_arch_common/server_connectivity.h"   // for Gen2ArchServerConnectivity
#include "platform/gen2_arch_common/runtime_connectivity.h"  // for Gen2ArchRuntimeConnectivity
#include "platform/gaudi3/hls3rack_runtime_connectivity.h"   // for HLS3RackRuntimeConnectivity
#include "platform/gaudi3/connectivity_autogen_HLS3Rack.h"   // for g_HLS3RackServerConnectivityVector

#include "hcl_utils.h"        // for LOG_HCL_*
#include "hcl_log_manager.h"  // for LOG_*

HLS3RackServerConnectivity::HLS3RackServerConnectivity(const int        fd,
                                                       const int        moduleId,
                                                       const bool       useDummyConnectivity,
                                                       HclDeviceConfig& deviceConfig)
: Gaudi3BaseServerConnectivity(fd,
                               moduleId,
                               useDummyConnectivity,
                               useDummyConnectivity ? g_dummyTestDeviceServerNicsConnectivity
                                                    : g_HLS3RackServerConnectivityVector,
                               deviceConfig)
{
}

Gen2ArchRuntimeConnectivity*
HLS3RackServerConnectivity::createRuntimeConnectivityFactory(const int                   moduleId,
                                                             const HCL_Comm              hclCommId,
                                                             Gen2ArchServerConnectivity& serverConnectivity)
{
    LOG_HCL_DEBUG(HCL, "Started, hclCommId={}", hclCommId);
    return new HLS3RackRuntimeConnectivity(moduleId, hclCommId, serverConnectivity);

    return nullptr;
}
