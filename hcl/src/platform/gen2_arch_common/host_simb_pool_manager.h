#pragma once

#include <cstdint>
#include <vector>

#include "device_simb_pool_manager.h"

class HostSimbPoolManager : public SimbPoolManagerBase<e_hostPoolID>
{
public:
    explicit HostSimbPoolManager(const uint64_t                          mappedBaseAddr,
                                 const uint64_t                          hostBaseAddr,
                                 const std::map<e_hostPoolID, unsigned>& sizes,
                                 const uint64_t                          singleBufferSize);
    virtual ~HostSimbPoolManager() = default;

    uint64_t getCurrentBuffer(const e_hostPoolID poolIdx) override;
    uint64_t allocNextBuffer(uint64_t targetValue, const e_hostPoolID poolIdx) override;
    uint64_t getCurrentMappedBuffer(const e_hostPoolID poolIdx);

protected:
    const uint64_t m_hostBaseAddr;
};
