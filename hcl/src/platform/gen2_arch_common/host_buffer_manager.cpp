#include "host_buffer_manager.h"

#include "hcl_utils.h"        // for VERIFY
#include "hcl_log_manager.h"  // for LOG_*

HostBufferManager::HostBufferManager(const uint64_t               mappedBaseAddr,
                                     const uint64_t               hostBaseAddr,
                                     const std::vector<unsigned>& sizes,
                                     const uint64_t               singleBufferSize)
: BufferManagerBase(std::array<BufferParams, 1> {{{mappedBaseAddr, singleBufferSize, 0, 2}}}, sizes),
  m_hostBaseAddr(hostBaseAddr)

{
    m_bufferParams[0].m_totalPoolsAmount = 0;
    for (unsigned i = 0; i < m_poolSizes.size(); ++i)
    {
        m_poolBases.push_back(m_bufferParams[0].m_totalPoolsAmount);
        m_creditManagers.emplace_back(m_poolSizes[i]);
        m_bufferParams[0].m_totalPoolsAmount += m_poolSizes[i];
    }
}

uint64_t HostBufferManager::getCurrentMappedBuffer(const e_hostPoolID poolIdx)
{
    const int idx = m_creditManagers[poolIdx].getCurrentCredit();
    if (idx < 0)
    {
        return -1;
    }

    return getBufferBaseAddr() + (idx + m_poolBases[poolIdx]) * getSingleBufferSize();
}

uint64_t HostBufferManager::getCurrentBuffer(const e_hostPoolID poolIdx)
{
    int idx = m_creditManagers[poolIdx].getCurrentCredit();
    if (idx < 0)
    {
        return -1;
    }

    return m_hostBaseAddr + (idx + m_poolBases[poolIdx]) * getSingleBufferSize();
}

uint64_t HostBufferManager::allocNextBuffer(uint64_t targetValue, const e_hostPoolID poolIdx)
{
    return m_creditManagers[poolIdx].allocNextCredit(targetValue);
}
