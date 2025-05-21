#include "simb_pool_container_allocator.h"

#include <cstdint>
#include <cstddef>
#include "hcl_utils.h"             // for VERIFY
#include "synapse_common_types.h"  // for synStatus
#include "hcl_types.h"             // for SYN_VALID_DEVICE_ID

// struct IntermediateBuffersAmount Moved to header file since its used by external cpp modules for G2 Hnics send/recv

SimbPoolContainerAllocator::SimbPoolContainerAllocator(uint64_t numberOfStreams) : m_numberOfStreams(numberOfStreams) {}

void SimbPoolContainerAllocator::destroy()
{
    for (unsigned poolContainerIndex = 0; poolContainerIndex < m_poolContainerParams.size(); poolContainerIndex++)
    {
        freeDeviceMemory(m_poolContainerParams[poolContainerIndex].containerBaseAddr);
    }

    if (m_fwBaseAddr)
    {
        freeDeviceMemory(m_fwBaseAddr);
    }
}

void SimbPoolContainerAllocator::generateContainerParams(unsigned                           simbSize,
                                                         const std::vector<e_devicePoolID>& pools,
                                                         PoolContainerParams&               poolContainerParams)
{
    poolContainerParams.simbSize                 = simbSize;
    poolContainerParams.simbCountPerStream       = SimbPoolContainerAllocator::getTotalSimbCount(pools);
    poolContainerParams.sizeOfContainerPerStream = simbSize * poolContainerParams.simbCountPerStream;
    poolContainerParams.sizeOfContainer          = poolContainerParams.sizeOfContainerPerStream * m_numberOfStreams;

    // the sibo index in fw is 9 bits
    VERIFY(poolContainerParams.simbCountPerStream * m_numberOfStreams <= ((1 << 9) - 1),
           "Max SIMB count for a container is 511, curr={}, simbSize={}",
           poolContainerParams.simbCountPerStream * m_numberOfStreams,
           poolContainerParams.simbSize);

    // Make sure each pool number is divisible by its factor (for buffer granularity)
    VERIFY(verifySIBPoolSizes(pools));
}

uint32_t SimbPoolContainerAllocator::getSizeOfAllBuffers(unsigned poolContainerIndex) const
{
    return m_poolContainerParams[poolContainerIndex].sizeOfContainer;
}

uint64_t SimbPoolContainerAllocator::getBufferSize() const
{
    return m_imbSize;
}

DeviceSimbPoolManagerBase& SimbPoolContainerAllocator::getDeviceSimbPoolManager(uint32_t streamIndex)
{
    return *(m_deviceSimbPoolManagers.at(streamIndex));
}

uint64_t SimbPoolContainerAllocator::getPoolContainerBaseAddr(unsigned poolContainerIndex)
{
    return m_poolContainerParams[poolContainerIndex].containerBaseAddr;
}

uint64_t SimbPoolContainerAllocator::getFwBaseAddr()
{
    return m_fwBaseAddr;
}

unsigned SimbPoolContainerAllocator::getSimbSize(unsigned poolContainerIndex) const
{
    return m_poolContainerParams[poolContainerIndex].simbSize;
}

unsigned SimbPoolContainerAllocator::getFwSliceSize()
{
    return m_fwImbSize;
}

unsigned SimbPoolContainerAllocator::getTotalSimbCount(const std::vector<e_devicePoolID>& pools)
{
    unsigned count = 0;
    for (const auto& pool : pools)
    {
        count += SimbCountInPool::getSimbCount(pool);
    }
    return count;
}

std::map<e_devicePoolID, unsigned>
SimbPoolContainerAllocator::getSIBMap(std::array<std::vector<e_devicePoolID>, MAX_POOL_CONTAINER_IDX>& poolTypes)
{
    std::map<e_devicePoolID, unsigned> SIBMap = {};
    for (unsigned i = 0; i < MAX_POOL_CONTAINER_IDX; i++)
    {
        for (e_devicePoolID pool : poolTypes[i])
        {
            SIBMap[pool] = SimbCountInPool::getSimbCount(pool);
        }
    }
    return SIBMap;
}

bool SimbPoolContainerAllocator::verifySIBPoolSizes(const std::vector<e_devicePoolID>& pools)
{
    for (const e_devicePoolID& pool : pools)
    {
        unsigned factor = DeviceSimbPoolManagerBase::getFactor(pool);
        if ((SimbCountInPool::getSimbCount(pool) % factor) != 0)
        {
            return false;
        }
    }
    return true;
}
