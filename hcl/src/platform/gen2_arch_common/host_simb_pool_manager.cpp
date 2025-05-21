#include "host_simb_pool_manager.h"

#include "hcl_utils.h"        // for VERIFY
#include "hcl_log_manager.h"  // for LOG_*

HostSimbPoolManager::HostSimbPoolManager(const uint64_t                          mappedBaseAddr,
                                         const uint64_t                          hostBaseAddr,
                                         const std::map<e_hostPoolID, unsigned>& sizes,
                                         const uint64_t                          singleBufferSize)
: SimbPoolManagerBase(std::array<SimbPoolContainerParamsPerStream, 1> {{{mappedBaseAddr, singleBufferSize, 0, 2}}},
                      sizes),
  m_hostBaseAddr(hostBaseAddr)

{
    m_spcParamsPerStream[0].m_simbCountPerStream = 0;
    for (const auto& poolEntry : m_poolSizes)
    {
        m_poolBases.emplace(poolEntry.first, m_spcParamsPerStream[0].m_simbCountPerStream);
        m_creditManagers.emplace(poolEntry.first, poolEntry.second);
        m_spcParamsPerStream[0].m_simbCountPerStream += poolEntry.second;
    }
}

uint64_t HostSimbPoolManager::getCurrentMappedBuffer(const e_hostPoolID poolIdx)
{
    const int idx = m_creditManagers[poolIdx].getCurrentCredit();
    if (idx < 0)
    {
        return -1;
    }

    return getStreamBaseAddressInContainer() + (idx + m_poolBases[poolIdx]) * getSimbSize();
}

uint64_t HostSimbPoolManager::getCurrentBuffer(const e_hostPoolID poolIdx)
{
    int idx = m_creditManagers[poolIdx].getCurrentCredit();
    if (idx < 0)
    {
        return -1;
    }

    return m_hostBaseAddr + (idx + m_poolBases[poolIdx]) * getSimbSize();
}

uint64_t HostSimbPoolManager::allocNextBuffer(uint64_t targetValue, const e_hostPoolID poolIdx)
{
    return m_creditManagers[poolIdx].allocNextCredit(targetValue);
}
