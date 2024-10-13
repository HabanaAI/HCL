#include "platform/gaudi3/hls3pcie_runtime_connectivity.h"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint*_t

#include "hcl_api_types.h"                                     // for HCL_Comm
#include "platform/gen2_arch_common/server_connectivity.h"     // for Gen2ArchServerConnectivity
#include "platform/gaudi3/gaudi3_base_runtime_connectivity.h"  // for Gaudi3BaseRuntimeConnectivity
#include "platform/gaudi3/gaudi3_base_server_connectivity.h"   // for Gaudi3BaseServerConnectivity

#include "hcl_utils.h"        // for VERIFY
#include "hcl_log_manager.h"  // for LOG_*

HLS3PCIERuntimeConnectivity::HLS3PCIERuntimeConnectivity(const int                   moduleId,
                                                         const HCL_Comm              hclCommId,
                                                         Gen2ArchServerConnectivity& serverConnectivity)
: Gaudi3BaseRuntimeConnectivity(moduleId, hclCommId, serverConnectivity)
{
    LOG_HCL_DEBUG(HCL, "ctor called, hclCommId={}", hclCommId);
}

static constexpr uint32_t mmD1_NIC0_QM_SPECIAL_GLBL_SPARE_0 = 0xD409F60;

// Needs to be adjusted per active scaleup ports
uint32_t HLS3PCIERuntimeConnectivity::getBackpressureOffset(const uint16_t nic) const
{
    return mmD1_NIC0_QM_SPECIAL_GLBL_SPARE_0;
}
