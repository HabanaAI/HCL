#include "platform/gaudi3/nics_events_handler_impl.h"

#include <cstdint>  // for uint*

#include "platform/gen2_arch_common/nics_events_hanlder_callback.h"  // for NicLkdEventsEnum
#include "platform/gaudi3/hcl_device.h"                              // for HclDeviceGaudi3
#include "hcl_utils.h"                                               // for VERIFY
#include "hcl_log_manager.h"                                         // for LOG_*

NicsEventsHandlerGaudi3::NicsEventsHandlerGaudi3(Gaudi3BaseServerConnectivity& serverConnectivity,
                                                 HclDeviceGaudi3&              device)
: NicsEventHandler(device), m_serverConnectivity(serverConnectivity)
{
}

void NicsEventsHandlerGaudi3::nicStatusChange(const uint16_t nic, const NicLkdEventsEnum status)
{
    LOG_HCL_INFO(HCL_FAILOVER, "nic={}, status={}", nic, status);
    // Handle common scaleup/scaleout nic status change here - async comm error

    if (m_serverConnectivity.isScaleoutPort(nic))
    {
        scaleoutNicStatusChange(nic, status);
    }
}

void NicsEventsHandlerGaudi3::scaleoutNicStatusChange(const uint16_t nic, const NicLkdEventsEnum status)
{
    LOG_HCL_DEBUG(HCL_FAILOVER, "nic={}, status={}", nic, status);
    switch (status)
    {
        case NicLkdEventsEnum::NIC_LKD_EVENTS_UP:
            ((HclDeviceGaudi3&)(m_device)).faultHandleScaleoutNicUp(nic);
            break;
        case NicLkdEventsEnum::NIC_LKD_EVENTS_DOWN:
            // Ignore down status
            break;
        case NicLkdEventsEnum::NIC_LKD_EVENTS_SHUTDOWN:
            ((HclDeviceGaudi3&)(m_device)).faultHandleScaleoutNicShutdown(nic);
            break;
    }
}
