#pragma once

#include <cstdint>  // for uint64_t, uint16_t
#include <array>    // for array
#include <vector>   // for vector

#include "hcl_api_types.h"                                   // for HCL_Comm
#include "platform/gaudi3/nic_passthrough_handler.h"         // for NicPassthroughHandlerGaudi3
#include "platform/gaudi3/port_mapping.h"                    // for Gaudi3DevicePortMapping
#include "platform/gen2_arch_common/send_recv_aggregator.h"  // for SendRecvAggregatorBase
#include "platform/gaudi3/port_mapping.h"                    // for Gaudi3DevicePortMapping

class HclCommandsGaudi3;
namespace hcl
{
class ScalStreamBase;
}

class SendRecvAggregatorGaudi3 : public SendRecvAggregatorBase
{
public:
    SendRecvAggregatorGaudi3(const bool                     isSend,
                             const uint32_t                 selfModuleId,
                             const Gaudi3DevicePortMapping& portMapping,
                             HclCommandsGaudi3&             commands);
    virtual ~SendRecvAggregatorGaudi3()                       = default;
    SendRecvAggregatorGaudi3(SendRecvAggregatorGaudi3&&)      = delete;
    SendRecvAggregatorGaudi3(const SendRecvAggregatorGaudi3&) = delete;
    SendRecvAggregatorGaudi3& operator=(SendRecvAggregatorGaudi3&&) = delete;
    SendRecvAggregatorGaudi3& operator=(const SendRecvAggregatorGaudi3&) = delete;

    void addSendRecvArray(const SendRecvArray& arr);
    void flush(hcl::ScalStreamBase& scalStream,
               const uint8_t        dcore,
               const uint8_t        ssm,
               const uint16_t       sobId,
               const uint32_t       qpn);

private:
    const bool                     m_isSend;
    const uint32_t                 m_selfModuleId;
    const Gaudi3DevicePortMapping& m_portMapping;
    HclCommandsGaudi3&             m_commands;
    NicPassthroughHandlerGaudi3    m_nicPassthroughHandlerSet0;
    NicPassthroughHandlerGaudi3    m_nicPassthroughHandlerSet1;
};
