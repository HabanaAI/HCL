#include "nics_events_handler_impl.h"

#include <cstdint>  // for uint*

#include "platform/gen2_arch_common/server_connectivity.h"  // for Gen2ArchServerConnectivity
#include "platform/gen2_arch_common/hcl_device.h"           // for HclDeviceGen2Arch
#include "hcl_utils.h"                                      // for VERIFY
#include "hcl_log_manager.h"                                // for LOG_*
#include "fault_tolerance_inc.h"                            // for HLFT.* macros

NicsEventHandler::NicsEventHandler(HclDeviceGen2Arch& device) : m_device(device) {}

void NicsEventHandler::nicStatusChange(const uint16_t nic, const NicLkdEventsEnum status)
{
    HLFT_INF("nic={}, status={}", nic, status);
}
