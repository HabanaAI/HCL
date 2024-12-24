#pragma once

#include "platform/gen2_arch_common/intermediate_buffer_container.h"  // for IntermediateBufferContainer

class IntermediateBufferContainerGaudiCommon : public IntermediateBufferContainer
{
public:
    IntermediateBufferContainerGaudiCommon(uint64_t numberOfStreams);
    ~IntermediateBufferContainerGaudiCommon() = default;

    bool allocateDeviceMemory(const uint64_t size, uint64_t* buffer) override;
    void freeDeviceMemory(uint64_t buffer) override;
    void allocateFwIntermediateBuffer() override;
};
