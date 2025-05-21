#pragma once

#include "platform/gen2_arch_common/simb_pool_container_allocator.h"  // for SimbPoolContainerAllocator

class SimbPoolContainerAllocatorGaudiCommon : public SimbPoolContainerAllocator
{
public:
    SimbPoolContainerAllocatorGaudiCommon(uint64_t numberOfStreams);
    ~SimbPoolContainerAllocatorGaudiCommon() = default;

    bool allocateDeviceMemory(const uint64_t size, uint64_t* buffer) override;
    void freeDeviceMemory(uint64_t buffer) override;
    void allocateFwIntermediateBuffer() override;
};
