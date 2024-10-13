#pragma once

#include <cstdint>  // for uint64_t, uint16_t
#include <array>    // for array
#include <vector>   // for vector

#include "hcl_api_types.h"                                   // for HCL_Comm
#include "platform/gaudi3/nic_passthrough_handler.h"         // for NicPassthroughHandlerGaudi3
#include "platform/gen2_arch_common/send_recv_aggregator.h"  // for SendRecvAggregatorBase

class HclCommandsGaudi3;
namespace hcl
{
class ScalStreamBase;
}
class Gaudi3DevicePortMapping;
class Gaudi3BaseServerConnectivity;

class SendRecvAggregatorGaudi3 : public SendRecvAggregatorBase
{
public:
    SendRecvAggregatorGaudi3(const bool                          isSend,
                             const uint32_t                      selfModuleId,
                             const Gaudi3BaseServerConnectivity& serverConnectivity,
                             const DevicesSet&                   hwModules,
                             HclCommandsGaudi3&                  commands);
    virtual ~SendRecvAggregatorGaudi3()                                  = default;
    SendRecvAggregatorGaudi3(SendRecvAggregatorGaudi3&&)                 = delete;
    SendRecvAggregatorGaudi3(const SendRecvAggregatorGaudi3&)            = delete;
    SendRecvAggregatorGaudi3& operator=(SendRecvAggregatorGaudi3&&)      = delete;
    SendRecvAggregatorGaudi3& operator=(const SendRecvAggregatorGaudi3&) = delete;

    void addSendRecvArray(const SendRecvArray& arr);
    void flush(hcl::ScalStreamBase& scalStream,
               const HCL_Comm       comm,
               const uint8_t        dcore,
               const uint8_t        ssm,
               const uint16_t       sobId,
               const uint32_t       qpn);

private:
    const bool                          m_isSend;
    const uint32_t                      m_selfModuleId;
    const Gaudi3BaseServerConnectivity& m_serverConnectivity;
    const DevicesSet&                   m_hwModules;
    HclCommandsGaudi3&                  m_commands;
    NicPassthroughHandlerGaudi3         m_nicPassthroughHandlerSet0;
    NicPassthroughHandlerGaudi3         m_nicPassthroughHandlerSet1;
};
