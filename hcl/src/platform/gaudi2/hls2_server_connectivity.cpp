#include "platform/gaudi2/hls2_server_connectivity.h"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint*_t

#include "hcl_api_types.h"                                   // for HCL_Comm
#include "platform/gaudi2/hls2_runtime_connectivity.h"       // for HLS2RuntimeConnectivity
#include "platform/gen2_arch_common/runtime_connectivity.h"  // for Gen2ArchRuntimeConnectivity
#include "platform/gen2_arch_common/server_connectivity.h"   // for Gen2ArchServerConnectivity
#include "platform/gaudi2/connectivity_autogen_HLS2.h"       // for g_HLS2ServerConnectivityArray

#include "hcl_utils.h"        // for LOG_HCL_*
#include "hcl_log_manager.h"  // for LOG_*

HLS2ServerConnectivity::HLS2ServerConnectivity(const int        fd,
                                               const int        moduleId,
                                               const bool       useDummyConnectivity,
                                               HclDeviceConfig& deviceConfig)
: Gen2ArchServerConnectivity(fd,
                             moduleId,
                             useDummyConnectivity,
                             useDummyConnectivity ? g_dummyTestDeviceServerNicsConnectivity
                                                  : g_HLS2ServerConnectivityArray,
                             deviceConfig)
{
}

Gen2ArchRuntimeConnectivity*
HLS2ServerConnectivity::createRuntimeConnectivityFactory(const int                   moduleId,
                                                         const HCL_Comm              hclCommId,
                                                         Gen2ArchServerConnectivity& serverConnectivity)
{
    LOG_HCL_DEBUG(HCL, "Started, hclCommId={}", hclCommId);
    return new HLS2RuntimeConnectivity(moduleId, hclCommId, serverConnectivity);

    return nullptr;
}
