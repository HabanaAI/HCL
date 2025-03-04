#include "platform/gaudi2/hls2_runtime_connectivity.h"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint8_t
#include <memory>   // for allocator_traits<>::value_type

#include "hcl_api_types.h"                                   // for HCL_Comm
#include "platform/gen2_arch_common/server_connectivity.h"   // for Gen2ArchServerConnectivity
#include "platform/gen2_arch_common/runtime_connectivity.h"  // for Gen2ArchRuntimeConnectivity
#include "gaudi2/asic_reg/nic0_qm_arc_aux0_regs.h"           // for mmNIC0_QM_ARC_AUX0_SCRATCHPAD_7

#include "hcl_utils.h"        // for LOG_HCL_*
#include "hcl_log_manager.h"  // for LOG_*

HLS2RuntimeConnectivity::HLS2RuntimeConnectivity(const int                   moduleId,
                                                 const HCL_Comm              hclCommId,
                                                 Gen2ArchServerConnectivity& serverConnectivity)
: Gen2ArchRuntimeConnectivity(moduleId, hclCommId, serverConnectivity)
{
    LOG_HCL_DEBUG(HCL, "ctor called, hclCommId={}", hclCommId);
}

// Needs to be adjusted per active scaleup ports
uint32_t HLS2RuntimeConnectivity::getBackpressureOffset(const uint16_t nic) const
{
    uint32_t bp_offs = mmNIC0_QM_ARC_AUX0_SCRATCHPAD_7;
    /* specific NIC ARC-AUX base (for even number) */
    bp_offs += (0x80000 * (nic / 2));
    /* specific NIC ARC-AUX base (for odd number) */
    bp_offs += (0x20000 * (nic & 0x1));  // (0x20000 * (nic % 2))
    return bp_offs;
}

void HLS2RuntimeConnectivity::initServerSpecifics() {}