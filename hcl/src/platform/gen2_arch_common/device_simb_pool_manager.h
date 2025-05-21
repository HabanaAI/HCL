
#pragma once
#include <cstdint>  // for int64_t, uint64_t, uint32_t
#include <vector>   // for vector

#include "simb_pool_manager_base.h"
#include "hccl_types.h"  // for hcclRedOp_t

constexpr unsigned MAX_SCALEOUT_FACTOR = 8;
constexpr unsigned MIN_SCALEOUT_FACTOR = 1;

class HclDeviceGen2Arch;
struct BufferToken;

class DeviceSimbPoolManagerBase : public SimbPoolManagerBase<e_devicePoolID, MAX_POOL_CONTAINER_IDX>
{
public:
    virtual ~DeviceSimbPoolManagerBase() = default;

    DeviceSimbPoolManagerBase(std::array<SimbPoolContainerParamsPerStream, MAX_POOL_CONTAINER_IDX> spcParamsPerStream,
                              const std::map<e_devicePoolID, unsigned>&                            sizes);
    DeviceSimbPoolManagerBase(DeviceSimbPoolManagerBase&&)                 = default;  // ALLOW move ctor
    DeviceSimbPoolManagerBase(const DeviceSimbPoolManagerBase&)            = delete;
    DeviceSimbPoolManagerBase& operator=(DeviceSimbPoolManagerBase&&)      = delete;
    DeviceSimbPoolManagerBase& operator=(const DeviceSimbPoolManagerBase&) = delete;

    void     init();
    uint64_t getCurrentBuffer(const e_devicePoolID poolIdx) override;
    uint64_t allocNextBuffer(uint64_t targetValue, const e_devicePoolID poolIdx) override;
    int64_t  getCurrentTargetValue(const e_devicePoolID poolIdx, const hcclRedOp_t reduceOp);

    uint64_t getBufferTotalSize() const;
    uint32_t getSliceId(e_devicePoolID poolIdx, uint32_t streamId);
    uint32_t getCurrentBufferIdx(e_devicePoolID poolIdx);
    uint64_t getBufferBaseAddr(const e_devicePoolID poolIdx) const;
    unsigned getPoolBufferSize(const e_devicePoolID poolIdx) const;
    uint64_t getStreamBaseAddrInContainer(const unsigned poolContainerIndex) const;
    uint64_t getSingleBufferSize(const e_devicePoolID poolIdx) const;
    uint64_t getSimbSize(const unsigned poolContainerIndex) const;

    void                              advanceProg(uint64_t currTargetValue);
    bool                              bufferExpired(e_devicePoolID poolId);
    unsigned                          getPoolContainerIndexByAddr(uint64_t address);
    virtual unsigned                  getPoolContainerIndex(const e_devicePoolID poolIdx) const = 0;
    SimbPoolContainerParamsPerStream& getPoolContainerParamsPerStream(const unsigned poolContainerIndex);
    static const unsigned             getFactor(const e_devicePoolID poolIdx);
    bool                              isPoolAllocated(e_devicePoolID poolIdx);
    static e_devicePoolID             fetchPool(const BufferToken& bufferHandle);
    static bool                       isSiboPool(const e_devicePoolID poolIdx);

private:
    // Granularity requirements for buffers:
    // 8 for scaleup buffers pool
    // All values must be a power of 2
    static const unsigned s_defaultFactor = 1;
    static const unsigned s_scaleupFactor = 8;
};
