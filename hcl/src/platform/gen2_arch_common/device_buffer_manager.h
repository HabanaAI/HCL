
#pragma once
#include <cstdint>  // for int64_t, uint64_t, uint32_t
#include <vector>   // for vector

#include "buffer_manager_base.h"
#include "hccl_types.h"  // for hcclRedOp_t

constexpr unsigned MAX_SCALEOUT_FACTOR = 8;

class HclDeviceGen2Arch;

class sibAddressAndSize
{
public:
    uint64_t sibSize;
    uint64_t sibBaseAddr;
    uint64_t sibAmount;

    sibAddressAndSize(uint64_t addr, uint64_t size, uint64_t poolAmount)
    {
        sibBaseAddr = addr;
        sibSize     = size;
        sibAmount   = poolAmount;
    }
};

class DeviceBufferManager : public BufferManagerBase<e_devicePoolID, MAX_NUM_POOL_SIZES>
{
public:
    virtual ~DeviceBufferManager() = default;

    DeviceBufferManager(std::array<BufferParams, MAX_NUM_POOL_SIZES> bufferParams, const std::vector<unsigned>& sizes);
    DeviceBufferManager(DeviceBufferManager&&)                 = default;  // ALLOW move ctor
    DeviceBufferManager(const DeviceBufferManager&)            = delete;
    DeviceBufferManager& operator=(DeviceBufferManager&&)      = delete;
    DeviceBufferManager& operator=(const DeviceBufferManager&) = delete;

    uint64_t getCurrentBuffer(const e_devicePoolID poolIdx) override;
    uint64_t allocNextBuffer(uint64_t targetValue, const e_devicePoolID poolIdx) override;
    int64_t  getCurrentTargetValue(const e_devicePoolID poolIdx, const hcclRedOp_t reduceOp);

    uint64_t getBufferTotalSize() const;
    uint32_t getSliceId(e_devicePoolID poolIdx, uint32_t streamId);
    uint32_t getCurrentBufferIdx(e_devicePoolID poolIdx);
    uint64_t getBufferBaseAddr(const e_devicePoolID poolIdx) const;
    unsigned getPoolBufferSize(const e_devicePoolID poolIdx) const;
    uint64_t getBufferBaseAddr(const unsigned index) const;
    uint64_t getSingleBufferSize(const e_devicePoolID poolIdx) const;
    uint64_t getSingleBufferSize(const unsigned index) const;

    void                  advanceProg(uint64_t currTargetValue);
    bool                  bufferExpired(e_devicePoolID poolId);
    unsigned              getPoolSizeIndexByAddr(uint64_t address);
    static unsigned       getPoolSizeIndex(const e_devicePoolID poolIdx);
    uint64_t              getBufferAmountInPool(unsigned poolId);
    static const unsigned getFactor(const e_devicePoolID poolIdx);

private:
    // Granularity requirements for buffers:
    // 8 for scaleup buffers pool
    // All values must be a power of 2
    static const unsigned s_defaultFactor = 1;
    static const unsigned s_scaleupFactor = 8;
};
