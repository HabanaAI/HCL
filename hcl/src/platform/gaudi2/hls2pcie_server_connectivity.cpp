#include "platform/gaudi2/hls2pcie_server_connectivity.h"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint*_t

#include "hcl_api_types.h"                                   // for HCL_Comm
#include "platform/gaudi2/hls2pcie_runtime_connectivity.h"   // for HLS2PCIERuntimeConnectivity
#include "platform/gen2_arch_common/runtime_connectivity.h"  // for Gen2ArchRuntimeConnectivity
#include "platform/gen2_arch_common/server_connectivity.h"   // for Gen2ArchServerConnectivity
#include "platform/gaudi2/connectivity_autogen_HLS2PCIE.h"   // for g_HLS2PCIEServerConnectivityVector

#include "hcl_utils.h"        // for LOG_HCL_*
#include "hcl_log_manager.h"  // for LOG_*

HLS2PCIEServerConnectivity::HLS2PCIEServerConnectivity(const int        fd,
                                                       const int        moduleId,
                                                       const bool       useDummyConnectivity,
                                                       HclDeviceConfig& deviceConfig)
: Gen2ArchServerConnectivity(fd,
                             moduleId,
                             useDummyConnectivity,
                             useDummyConnectivity ? g_dummyTestDeviceServerNicsConnectivity
                                                  : g_HLS2PCIEServerConnectivityVector,
                             deviceConfig)
{
}

Gen2ArchRuntimeConnectivity*
HLS2PCIEServerConnectivity::createRuntimeConnectivityFactory(const int                   moduleId,
                                                             const HCL_Comm              hclCommId,
                                                             Gen2ArchServerConnectivity& serverConnectivity)
{
    LOG_HCL_DEBUG(HCL, "Started, hclCommId={}", hclCommId);
    return new HLS2PCIERuntimeConnectivity(moduleId, hclCommId, serverConnectivity);

    return nullptr;
}
