#pragma once

#include <cstdint>
#include <vector>

#include "device_buffer_manager.h"

class HostBufferManager : public BufferManagerBase<e_hostPoolID>
{
public:
    explicit HostBufferManager(const uint64_t               mappedBaseAddr,
                               const uint64_t               hostBaseAddr,
                               const std::vector<unsigned>& sizes,
                               const uint64_t               singleBufferSize);
    virtual ~HostBufferManager() = default;

    uint64_t getCurrentBuffer(const e_hostPoolID poolIdx) override;
    uint64_t  allocNextBuffer(uint64_t targetValue, const e_hostPoolID poolIdx) override;
    uint64_t getCurrentMappedBuffer(const e_hostPoolID poolIdx);

protected:
    const uint64_t m_hostBaseAddr;
};
