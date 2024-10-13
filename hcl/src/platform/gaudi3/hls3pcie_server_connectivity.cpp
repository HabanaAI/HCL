#include "platform/gaudi3/hls3pcie_server_connectivity.h"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint*_t

#include "hcl_api_types.h"                                   // for HCL_Comm
#include "platform/gen2_arch_common/server_connectivity.h"   // for Gen2ArchServerConnectivity
#include "platform/gen2_arch_common/runtime_connectivity.h"  // for Gen2ArchRuntimeConnectivity
#include "platform/gaudi3/hls3pcie_runtime_connectivity.h"   // for HLS3PCIERuntimeConnectivity
#include "platform/gaudi3/connectivity_autogen_HLS3PCIE.h"   // for g_HLS3PCIEServerConnectivityArray

#include "hcl_utils.h"        // for LOG_HCL_*
#include "hcl_log_manager.h"  // for LOG_*

HLS3PCIEServerConnectivity::HLS3PCIEServerConnectivity(const int        fd,
                                                       const int        moduleId,
                                                       const bool       useDummyConnectivity,
                                                       HclDeviceConfig& deviceConfig)
: Gaudi3BaseServerConnectivity(fd,
                               moduleId,
                               useDummyConnectivity,
                               useDummyConnectivity ? g_dummyTestDeviceServerNicsConnectivity
                                                    : g_HLS3PCIEServerConnectivityArray,
                               deviceConfig)
{
}

Gen2ArchRuntimeConnectivity*
HLS3PCIEServerConnectivity::createRuntimeConnectivityFactory(const int                   moduleId,
                                                             const HCL_Comm              hclCommId,
                                                             Gen2ArchServerConnectivity& serverConnectivity)
{
    LOG_HCL_DEBUG(HCL, "Started, hclCommId={}", hclCommId);
    return new HLS3PCIERuntimeConnectivity(moduleId, hclCommId, serverConnectivity);

    return nullptr;
}
