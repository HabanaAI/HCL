#pragma once

#include <cstdint>

enum NicLkdEventsEnum
{
    NIC_LKD_EVENTS_UP = 0,
    NIC_LKD_EVENTS_DOWN,
    NIC_LKD_EVENTS_SHUTDOWN
};

class INicsEventsHandlerCallBack
{
public:
    virtual void nicStatusChange(const uint16_t nic, const NicLkdEventsEnum status) = 0;
};
