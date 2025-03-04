#pragma once

#include "buffer_manager_base.h"
#include "device_buffer_manager.h"
#include "hcl_public_streams.h"

static constexpr uint32_t MAX_BUFFERS_TO_ALLOCATE {5};

class CommonState;

struct BufferAllocation
{
    e_devicePoolID m_poolId;
    unsigned       m_iterations;
    bool           dontWaitOnCg;
};

class BufferAllocationManager
{
public:
    BufferAllocationManager();

    void registerStaticBuffersAllocations(CommonState& commonState, unsigned boxIter);

    unsigned alloc(DeviceBufferManager&                deviceBufferManager,
                   hcl::syncInfo&                      longSo,
                   int64_t                             cgSize,
                   unsigned                            requiredExtraCredits,
                   std::vector<std::pair<bool, bool>>& ltuValid);

    bool isValid() const;
    void reset();
    void addAllocation(e_devicePoolID poolId, unsigned numIterations, bool dontWaitOnCg = false);
    void setRepetitions(unsigned repetitions);

protected:
    std::array<BufferAllocation, MAX_BUFFERS_TO_ALLOCATE> m_allocations;
    unsigned                                              m_repetitions;
    unsigned                                              m_nextBufferToAllocateIndex;
};