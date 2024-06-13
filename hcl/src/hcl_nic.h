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

class HclNic : public IHclNic
{
public:
    HclNic(IHclDevice* device, int nic, uint32_t nQPN, uint32_t memType, uint32_t sendType, uint32_t recvType);
    virtual ~HclNic() noexcept(false);

    virtual void init() override;

protected:
    uint32_t m_nQPN     = -1;
    uint32_t m_memType  = -1;
    uint32_t m_sendType = -1;
    uint32_t m_recvType = -1;

    void _wq_arr_set_();
};
