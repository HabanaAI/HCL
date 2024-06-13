#pragma once

#include <cstdint>                       // for uint8_t, uint32_t
#include "infra/hcl_affinity_manager.h"  // for HclThread
class IHclDevice;

// poll on the LKD event queue and handle relevant events
class IEventQueueHandler
{
public:
    IEventQueueHandler()          = default;
    virtual ~IEventQueueHandler() = default;

    void startThread(IHclDevice* device);
    void stopThread();
    void handler();

protected:
    virtual void parseQpErrorParams(uint32_t ev_data, uint8_t& source, uint8_t& cause, uint8_t& qpcSource) = 0;

    virtual char* getErrorCauseRXStrings(uint8_t errorCause)                          = 0;
    virtual char* getErrorCauseTXStrings(uint8_t errorCause)                          = 0;
    virtual char* getErrorCauseQPCStrings(uint8_t errorCause, uint8_t errorQpcSource) = 0;

    IHclDevice* m_device = nullptr;
    int         m_fd     = -1;

    HclThread m_thread;
    bool      m_stopEqThread = false;

private:
    /**
     * @brief Parse EQ events and print a relevant log
     *
     * @param eqe event queue element
     * @param port
     * @return true if event is fatal and should later trigger DFA
     * @return false otherwise
     */
    bool parseNicEventParams(const hlthunk_nic_eq_poll_out& eqe, const uint32_t port);
};
