#pragma once

#include "hcl_nic.h"
#include "ibverbs/hcl_ibverbs.h"


class Gen2ArchNic : public IHclNic
{
public:
    virtual void init() override ;

protected:
    Gen2ArchNic(IHclDevice* device, uint32_t nic, uint32_t nQPN, uint32_t bp, eNicType nt);

};
