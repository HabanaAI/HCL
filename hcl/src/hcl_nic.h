#pragma once

#include <cstdint>          // for uint32_t
#include <unordered_map>    // for unordered_map
#include "hcl_types.h"

class IHclDevice;

class IHclNic
{
public:
    IHclNic(uint32_t nic) : m_nic(nic) {};
    IHclNic(IHclDevice* device, uint32_t nic) : m_device(device), m_nic(nic) {};

    virtual void init() {};

protected:
    IHclDevice* m_device = nullptr;
    uint32_t    m_nic    = -1;
};
