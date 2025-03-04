
#pragma once

#include <cstdint>  // for uint*

#include "hcl_types.h"                                               // for NicLkdEventsEnum
#include "platform/gen2_arch_common/nics_events_hanlder_callback.h"  // for INicsEventsHandlerCallBack

class Gen2ArchServerConnectivity;
class HclDeviceGen2Arch;

class NicsEventHandler : public INicsEventsHandlerCallBack
{
public:
    NicsEventHandler(HclDeviceGen2Arch& device);
    virtual ~NicsEventHandler()                          = default;
    NicsEventHandler(const NicsEventHandler&)            = delete;
    NicsEventHandler& operator=(const NicsEventHandler&) = delete;

    virtual void nicStatusChange(const uint16_t nic, const NicLkdEventsEnum status)
        override;  // This function is normally called in context of different thread than user thread

protected:
    HclDeviceGen2Arch& m_device;
};
