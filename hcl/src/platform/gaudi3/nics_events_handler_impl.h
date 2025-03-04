
#pragma once

#include "hcl_types.h"                                           // for NicLkdEventsEnum
#include "platform/gen2_arch_common/nics_events_handler_impl.h"  // for NicsEventHandler
#include "platform/gaudi3/gaudi3_base_server_connectivity.h"     // for Gaudi3BaseServerConnectivity

#include <cstdint>  // for uint*

class HclDeviceGaudi3;

class NicsEventsHandlerGaudi3 : public NicsEventHandler
{
public:
    NicsEventsHandlerGaudi3(Gaudi3BaseServerConnectivity& serverConnectivity, HclDeviceGaudi3& device);
    virtual ~NicsEventsHandlerGaudi3() = default;

    void nicStatusChange(const uint16_t nic, const NicLkdEventsEnum status)
        override;  // This function is normally called in context of different thread than user thread

protected:
    void scaleoutNicStatusChange(const uint16_t nic, const NicLkdEventsEnum status);

private:
    Gaudi3BaseServerConnectivity& m_serverConnectivity;
};
