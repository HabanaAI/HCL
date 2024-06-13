#pragma once

#include "hcl_nic.h"
#include "ibverbs/hcl_ibverbs.h"


class Gen2ArchNic : public HclNic
{
protected:
    Gen2ArchNic(IHclDevice* device, int nic, uint32_t nQPN, uint32_t sendType, uint32_t recvType, uint32_t bp);

    void _set_app_params_();

    uint32_t m_bp = -1;
};

class Gen2ArchIBVNic : public IHclNic
{
public:
    virtual void init() override ;

protected:
    Gen2ArchIBVNic(IHclDevice* device, uint32_t nic, uint32_t nQPN, uint32_t bp, eNicType nt);

};
