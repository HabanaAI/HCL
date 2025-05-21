#include "simb_pool_manager_base.h"

#include <cstdint>
#include <numeric>

#include "hcl_global_conf.h"
#include "hcl_utils.h"        // for VERIFY
#include "hcl_log_manager.h"  // for LOG_*

template<typename T, size_t SIZE>
SimbPoolManagerBase<T, SIZE>::SimbPoolManagerBase(
    const std::array<SimbPoolContainerParamsPerStream, SIZE> spcParamsPerStream,
    const std::map<T, unsigned>&                             sizes)
: m_spcParamsPerStream(spcParamsPerStream), m_poolSizes(sizes)
{
}

template<typename T, size_t SIZE>
uint64_t SimbPoolManagerBase<T, SIZE>::getSimbSize() const
{
    return m_spcParamsPerStream.at(0).m_simbSize;
}

template<typename T, size_t SIZE>
uint64_t SimbPoolManagerBase<T, SIZE>::getStreamBaseAddressInContainer() const
{
    return m_spcParamsPerStream.at(0).m_streamBaseAddrInContainer;
}

template<typename T, size_t SIZE>
unsigned SimbPoolManagerBase<T, SIZE>::getPoolContainerCount()
{
    return SIZE;
}

template class SimbPoolManagerBase<e_devicePoolID, MAX_POOL_CONTAINER_IDX>;
template class SimbPoolManagerBase<e_hostPoolID>;
