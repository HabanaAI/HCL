#pragma once

#include <cstdint>                                  // for uint8_t, uint32_t
#include "platform/gen2_arch_common/eq_handler.h"   // for IEventQueueHandler

// poll on the LKD event queue and handle relevant events
class Gaudi3EventQueueHandler : public IEventQueueHandler
{
public:
    Gaudi3EventQueueHandler()          = default;
    virtual ~Gaudi3EventQueueHandler() = default;

private:
    virtual void  parseQpErrorParams(uint32_t ev_data, uint8_t& source, uint8_t& cause, uint8_t& qpcSource) override;
    virtual char* getErrorCauseRXStrings(uint8_t errorCause) override;
    virtual char* getErrorCauseTXStrings(uint8_t errorCause) override;
    virtual char* getErrorCauseQPCStrings(uint8_t errorCause, uint8_t errorQpcSource) override;
};
