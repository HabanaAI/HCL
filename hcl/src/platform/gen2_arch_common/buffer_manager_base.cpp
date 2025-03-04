#include "buffer_manager_base.h"

#include <cstdint>
#include <numeric>

#include "hcl_global_conf.h"
#include "hcl_utils.h"        // for VERIFY
#include "hcl_log_manager.h"  // for LOG_*

template<typename T, size_t SIZE>
BufferManagerBase<T, SIZE>::BufferManagerBase(const std::array<BufferParams, SIZE> bufferParams,
                                              const std::map<T, unsigned>&         sizes)
: m_bufferParams(bufferParams), m_poolSizes(sizes)
{
}

template<typename T, size_t SIZE>
uint64_t BufferManagerBase<T, SIZE>::getSingleBufferSize() const
{
    return m_bufferParams.at(0).m_singleBufferSize;
}

template<typename T, size_t SIZE>
uint64_t BufferManagerBase<T, SIZE>::getBufferBaseAddr() const
{
    return m_bufferParams.at(0).m_bufferBaseAddr;
}

template<typename T, size_t SIZE>
unsigned BufferManagerBase<T, SIZE>::getPoolAmount()
{
    return SIZE;
}

template class BufferManagerBase<e_devicePoolID, MAX_NUM_POOL_SIZES>;
template class BufferManagerBase<e_hostPoolID>;
