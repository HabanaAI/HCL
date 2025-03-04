#pragma once

#include <cstdint>  // for int64_t, uint64_t, uint32_t
#include <vector>   // for vector
#include <array>    // for array
#include <map>      // for map

#include "hccl_types.h"      // for hcclRedOp_t
#include "credit_manager.h"  // for CreditManager

enum e_hostPoolID
{
    HNIC_SEND_POOL  = 0,
    FIRST_HOST_POOL = HNIC_SEND_POOL,
    HNIC_RECV_POOL,
    LAST_HOST_POOL = HNIC_RECV_POOL
};

enum e_devicePoolID
{
    SCALEOUT_POOL_0 = 0,
    REDUCE_POOL,
    PRIMITIVE_POOL,
    SCALEUP_AND_ALL2ALL_POOL,
    SCALEOUT_GDR_POOL,  // dedicated for gaudi-direct recv from mlnx nics
    SCALEOUT_POOL_1,    // dedicated for RS SO Recv continuous reduction flow
    SCALEOUT_ACC_POOL,  // dedicated for RS SO recv continuous reduction flow - SO  buffer will be reduced to here every
                        // SCALEOUT_FACTOR iters
    NO_POOL = -1
};

constexpr e_devicePoolID SCALEOUT_POOL = SCALEOUT_POOL_0;

enum ePoolSizeIndex
{
    DOUBLE_SLICE_SIZE_POOL_IDX = 0,
    STANDARD_SLICE_SIZE_POOL_IDX,
    MAX_NUM_POOL_SIZES
};

constexpr unsigned INVALID_POOL_SIZE_IDX = MAX_NUM_POOL_SIZES;
// number of scaleout pools for RS SO recv continuous reduction flow
constexpr unsigned RS_CONT_REDUC_SO_POOL_AMOUNT = 2;

struct BufferParams
{
    uint64_t m_bufferBaseAddr   = 0;
    uint64_t m_singleBufferSize = 0;
    uint64_t m_totalPoolsAmount = 0;
    uint64_t m_numPoolTypes     = 0;
};

template<typename T, size_t SIZE = 1>
class BufferManagerBase
{
public:
    virtual ~BufferManagerBase() = default;

    BufferManagerBase(const std::array<BufferParams, SIZE> bufferParams, const std::map<T, unsigned>& sizes);
    BufferManagerBase(BufferManagerBase&&)                 = default;  // Allow move constructor
    BufferManagerBase(const BufferManagerBase&)            = delete;
    BufferManagerBase& operator=(BufferManagerBase&&)      = delete;
    BufferManagerBase& operator=(const BufferManagerBase&) = delete;

    virtual uint64_t getCurrentBuffer(const T poolIdx)                      = 0;
    virtual uint64_t allocNextBuffer(uint64_t targetValue, const T poolIdx) = 0;
    virtual unsigned getPoolAmount();

    uint64_t getSingleBufferSize() const;
    uint64_t getBufferBaseAddr() const;

protected:
    std::array<BufferParams, SIZE> m_bufferParams;
    const std::map<T, unsigned>    m_poolSizes;
    std::map<T, CreditManager>     m_creditManagers;
    std::map<T, unsigned>          m_poolBases;
};
