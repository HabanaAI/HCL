#pragma once
#include "platform/gaudi_common/simb_pool_container_allocator.h"

class SimbPoolContainerAllocatorGaudi3 : public SimbPoolContainerAllocatorGaudiCommon
{
public:
    SimbPoolContainerAllocatorGaudi3(uint64_t numberOfStreams);
    ~SimbPoolContainerAllocatorGaudi3() = default;

    virtual void init() override;

private:
};