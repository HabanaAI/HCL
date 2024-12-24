#pragma once

#include <cstdint>  // for uint*

#include "infra/hcl_affinity_manager.h"  // for HclThread

class IHclDevice;
class INicsEventsHandlerCallBack;

// poll on the LKD event queue and handle relevant events
class IEventQueueHandler
{
public:
    IEventQueueHandler(INicsEventsHandlerCallBack& nicsEventsHandlerCallback);
    virtual ~IEventQueueHandler()                            = default;
    IEventQueueHandler(const IEventQueueHandler&)            = delete;
    IEventQueueHandler& operator=(const IEventQueueHandler&) = delete;

    void startThread(IHclDevice* device);
    void stopThread();

protected:
    INicsEventsHandlerCallBack& m_nicsEventsHandlerCallback;
    IHclDevice*                 m_device = nullptr;
    int                         m_fd     = -1;

    HclThread m_thread;
    bool      m_stopEqThread = false;
};
