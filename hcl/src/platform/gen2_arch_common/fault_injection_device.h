#pragma once

#include "fault_injection_tcp_server.h"

#include <cstdint>  // for uint*

#include "platform/gen2_arch_common/server_connectivity.h"       // for Gen2ArchServerConnectivity
#include "platform/gen2_arch_common/nics_events_handler_impl.h"  // for NicsEventHandler

class FaultInjectionDevice : public FaultInjectionTcpServer
{
public:
    FaultInjectionDevice(const Gen2ArchServerConnectivity& serverConnectivity,
                         const uint32_t                    baseServerPort,
                         NicsEventHandler&                 nicsEventsHandler);
    virtual ~FaultInjectionDevice() = default;

private:
    virtual void portUp(const uint16_t portNum) override;      // Logical scaleout port number
    virtual void portDown(const uint16_t portNum) override;    // Logical scaleout port number
    virtual void nicUp(const uint16_t nicNum) override;        // physical scaleup/scaleout nic number
    virtual void nicDown(const uint16_t nicNum) override;      // physical scaleup/scaleout nic number
    virtual void nicShutdown(const uint16_t nicNum) override;  // physical scaleup/scaleout nic number

    virtual void stopAllApi() override;
    virtual void resumeAllApi() override;

    const Gen2ArchServerConnectivity& m_serverConnectivity;

    NicsEventHandler& m_nicsEventsHandler;
};
