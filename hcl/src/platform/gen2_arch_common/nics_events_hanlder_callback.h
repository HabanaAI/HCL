#pragma once

#include <cstdint>
#include "hcl_types.h"  // for NicLkdEventsEnum

class INicsEventsHandlerCallBack
{
public:
    virtual void nicStatusChange(const uint16_t nic, const NicLkdEventsEnum status) = 0;
};
