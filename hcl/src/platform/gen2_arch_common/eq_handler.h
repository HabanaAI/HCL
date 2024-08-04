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

protected:
    IHclDevice* m_device = nullptr;
    int         m_fd     = -1;

    HclThread m_thread;
    bool      m_stopEqThread = false;

};
