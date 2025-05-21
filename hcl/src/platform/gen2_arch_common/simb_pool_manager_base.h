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

enum ePoolContainerIndex
{
    SIBO_DOUBLE_SIMB_SIZE = 0,
    SIBO_STANDARD_SIMB_SIZE,
    NON_SIBO_DOUBLE_SIMB_SIZE,
    NON_SIBO_STANDARD_SIMB_SIZE,
    MAX_POOL_CONTAINER_IDX
};

constexpr unsigned INVALID_POOL_CONTAINER_IDX = MAX_POOL_CONTAINER_IDX;
// number of scaleout pools for RS SO recv continuous reduction flow
constexpr unsigned RS_CONT_REDUC_SO_POOL_AMOUNT = 2;

class SimbPoolContainerParamsPerStream
{
public:
    SimbPoolContainerParamsPerStream() = default;
    SimbPoolContainerParamsPerStream(uint64_t streamBaseAddrInContainer,
                                     uint64_t simbSize,
                                     uint64_t simbCountPerStream,
                                     uint64_t PoolTypesCnt)
    {
        m_streamBaseAddrInContainer = streamBaseAddrInContainer;
        m_simbSize                  = simbSize;
        m_simbCountPerStream        = simbCountPerStream;
        m_PoolTypesCnt              = PoolTypesCnt;
    }
    uint64_t m_streamBaseAddrInContainer = 0;
    uint64_t m_simbSize                  = 0;
    uint64_t m_simbCountPerStream        = 0;
    uint64_t m_PoolTypesCnt              = 0;
};

template<typename T, size_t SIZE = 1>
class SimbPoolManagerBase
{
public:
    virtual ~SimbPoolManagerBase() = default;

    SimbPoolManagerBase(const std::array<SimbPoolContainerParamsPerStream, SIZE> spcParamsPerStream,
                        const std::map<T, unsigned>&                             sizes);
    SimbPoolManagerBase(SimbPoolManagerBase&&)                 = default;  // Allow move constructor
    SimbPoolManagerBase(const SimbPoolManagerBase&)            = delete;
    SimbPoolManagerBase& operator=(SimbPoolManagerBase&&)      = delete;
    SimbPoolManagerBase& operator=(const SimbPoolManagerBase&) = delete;

    virtual uint64_t getCurrentBuffer(const T poolIdx)                      = 0;
    virtual uint64_t allocNextBuffer(uint64_t targetValue, const T poolIdx) = 0;
    virtual unsigned getPoolContainerCount();

    uint64_t getSimbSize() const;
    uint64_t getStreamBaseAddressInContainer() const;

protected:
    std::array<SimbPoolContainerParamsPerStream, SIZE> m_spcParamsPerStream;
    const std::map<T, unsigned>                        m_poolSizes;
    std::map<T, CreditManager>                         m_creditManagers;
    std::map<T, unsigned>                              m_poolBases;
};
