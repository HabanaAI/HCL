#include "intermediate_buffer_container.h"

#include <cstdint>
#include <cstddef>
#include "hcl_utils.h"             // for VERIFY
#include "synapse_common_types.h"  // for synStatus
#include "hcl_types.h"             // for SYN_VALID_DEVICE_ID

// struct IntermediateBuffersAmount Moved to header file since its used by external cpp modules for G2 Hnics send/recv

IntermediateBufferContainer::IntermediateBufferContainer(uint64_t numberOfStreams) : m_numberOfStreams(numberOfStreams)
{
}

void IntermediateBufferContainer::init()
{
    if (GCFG_HCCL_GAUDI_DIRECT.value() && !GCFG_HCL_IMB_SIZE.isSetFromUserConfig())
    {
        m_imbSize = GCFG_HCL_GDR_SLICE_SIZE.value();
        LOG_HCL_INFO(HCL,
                     "Using increased IMB size of {}MB since Gaudi-direct is enabled",
                     B2MB(GCFG_HCL_GDR_SLICE_SIZE.value()));
    }
    else
    {
        m_imbSize = GCFG_HCL_IMB_SIZE.value();
    }

    std::vector<e_devicePoolID> firstPool  = {SCALEOUT_POOL};
    std::vector<e_devicePoolID> secondPool = {REDUCE_POOL, SCALEUP_AND_ALL2ALL_POOL};

    m_firstPool = SCALEOUT_POOL;
    m_lastPool  = SCALEUP_AND_ALL2ALL_POOL;

    if (GCFG_HCCL_GAUDI_DIRECT.value())
    {
        secondPool.push_back(SCALEOUT_GDR_POOL);
        m_lastPool = SCALEOUT_GDR_POOL;
    }

    generatePoolParams(m_imbSize * 2, firstPool, m_bufferContainerParams[0]);
    generatePoolParams(m_imbSize, secondPool, m_bufferContainerParams[1]);

    for (unsigned poolSizeIndex = 0; poolSizeIndex < m_bufferContainerParams.size(); poolSizeIndex++)
    {
        LOG_HCL_TRACE(HCL,
                      "sizeOfSIB={}, sizeOfAllBuffers={}",
                      m_bufferContainerParams[poolSizeIndex].sizeOfSIB,
                      m_bufferContainerParams[poolSizeIndex].sizeOfAllBuffers);

        VERIFY(allocateDeviceMemory(m_bufferContainerParams[poolSizeIndex].sizeOfAllBuffers,
                                    &m_bufferContainerParams[poolSizeIndex].allBufferBaseAddr),
               "Failed to allocate device memory. Size: {:g}MB",
               B2MB(m_bufferContainerParams[poolSizeIndex].sizeOfAllBuffers));
        LOG_HCL_INFO(HCL,
                     "Allocated device memory. Address: 0x{:x}, Size: {:g}MB",
                     m_bufferContainerParams[poolSizeIndex].allBufferBaseAddr,
                     B2MB(m_bufferContainerParams[poolSizeIndex].sizeOfAllBuffers));
    }

    for (size_t i = 0; i < m_numberOfStreams; i++)
    {
        auto                                         firstPoolSizeParams  = m_bufferContainerParams[0];
        auto                                         secondPoolSizeParams = m_bufferContainerParams[1];
        std::array<BufferParams, MAX_NUM_POOL_SIZES> m_bufferParams       = {
            {{(firstPoolSizeParams.allBufferBaseAddr + (i * firstPoolSizeParams.sizeOfSIB)),
                    firstPoolSizeParams.sliceSize,
                    firstPoolSizeParams.countOfSIB,
                    firstPool.size()},
                   {(secondPoolSizeParams.allBufferBaseAddr + (i * secondPoolSizeParams.sizeOfSIB)),
                    secondPoolSizeParams.sliceSize,
                    secondPoolSizeParams.countOfSIB,
                    secondPool.size()}}};

        m_sibBuffers.emplace_back(DeviceBufferManager(m_bufferParams, getSIBVector()));
    }

    allocateFwIntermediateBuffer();
}

void IntermediateBufferContainer::destroy()
{
    for (unsigned poolSizeIndex = 0; poolSizeIndex < m_bufferContainerParams.size(); poolSizeIndex++)
    {
        freeDeviceMemory(m_bufferContainerParams[poolSizeIndex].allBufferBaseAddr);
    }

    if (m_fwBaseAddr)
    {
        freeDeviceMemory(m_fwBaseAddr);
    }
}

void IntermediateBufferContainer::generatePoolParams(unsigned                           sliceSize,
                                                     const std::vector<e_devicePoolID>& pools,
                                                     BufferContainerParams&             bufferContainerParams)
{
    bufferContainerParams.sliceSize        = sliceSize;
    bufferContainerParams.countOfSIB       = IntermediateBufferContainer::getSIBCount(pools);
    bufferContainerParams.sizeOfSIB        = sliceSize * bufferContainerParams.countOfSIB;
    bufferContainerParams.sizeOfAllBuffers = bufferContainerParams.sizeOfSIB * m_numberOfStreams;

    // Make sure each pool number is divisible by its factor (for buffer granularity)
    VERIFY(verifySIBPoolSizes(pools));
}

uint32_t IntermediateBufferContainer::getSizeOfAllBuffers(unsigned poolSizeIndex) const
{
    return m_bufferContainerParams[poolSizeIndex].sizeOfAllBuffers;
}

uint64_t IntermediateBufferContainer::getBufferSize() const
{
    return m_imbSize;
}

DeviceBufferManager& IntermediateBufferContainer::getSIB(uint32_t streamIndex)
{
    return m_sibBuffers.at(streamIndex);
}

uint64_t IntermediateBufferContainer::getAllBufferBaseAddr(unsigned poolSizeIndex)
{
    return m_bufferContainerParams[poolSizeIndex].allBufferBaseAddr;
}

uint64_t IntermediateBufferContainer::getFwBaseAddr()
{
    return m_fwBaseAddr;
}

unsigned IntermediateBufferContainer::getSliceSize(unsigned poolSizeIndex) const
{
    return m_bufferContainerParams[poolSizeIndex].sliceSize;
}

unsigned IntermediateBufferContainer::getFwSliceSize()
{
    return m_fwImbSize;
}

unsigned IntermediateBufferContainer::getSIBCount(const std::vector<e_devicePoolID>& pools)
{
    unsigned count = 0;
    for (const auto& pool : pools)
    {
        count += IntermediateBuffersAmount::getBufferCount(pool);
    }
    return count;
}

std::vector<unsigned> IntermediateBufferContainer::getSIBVector()
{
    std::vector<unsigned> SIBVector = {};
    for (int i = m_firstPool; i < m_lastPool + 1; i++)
    {
        SIBVector.push_back(IntermediateBuffersAmount::getBufferCount(static_cast<e_devicePoolID>(i)));
    }
    return SIBVector;
}

bool IntermediateBufferContainer::verifySIBPoolSizes(const std::vector<e_devicePoolID>& pools)
{
    for (const e_devicePoolID& pool : pools)
    {
        unsigned factor = DeviceBufferManager::getFactor(pool);
        if ((IntermediateBuffersAmount::getBufferCount(pool) % factor) != 0)
        {
            return false;
        }
    }
    return true;
}
