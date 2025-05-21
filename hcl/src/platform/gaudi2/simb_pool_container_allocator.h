#pragma once
#include "platform/gaudi_common/simb_pool_container_allocator.h"

class SimbPoolContainerAllocatorGaudi2 : public SimbPoolContainerAllocatorGaudiCommon
{
public:
    SimbPoolContainerAllocatorGaudi2(uint64_t numberOfStreams);
    ~SimbPoolContainerAllocatorGaudi2() = default;

    virtual void init() override;

private:
};