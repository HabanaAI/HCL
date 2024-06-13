#include "intermediate_buffer_container.h"

#include <cstdint>
#include <cstddef>
#include "hcl_utils.h"    // for VERIFY
#include "synapse_api.h"  // for synDeviceFree, synDeviceMalloc
#include "synapse_common_types.h"

using namespace hcl;

// struct IntermediateBuffersAmount Moved to header file since its used by external cpp modules for G2 Hnics send/recv

IntermediateBufferContainer::IntermediateBufferContainer(uint32_t deviceId, uint32_t numberOfStreams)
: m_deviceId(deviceId), m_numberOfStreams(numberOfStreams)
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

    std::vector<e_devicePoolID> firstPool  = {SCALEOUT_RR_POOL};
    std::vector<e_devicePoolID> secondPool = {REDUCE_RR_POOL, SCALEUP_RR_AND_ALL2ALL_POOL};

    m_firstPool = SCALEOUT_RR_POOL;
    m_lastPool  = SCALEUP_RR_AND_ALL2ALL_POOL;

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

        VERIFY(synSuccess == synDeviceMalloc(deviceId,
                                             m_bufferContainerParams[poolSizeIndex].sizeOfAllBuffers,
                                             0,
                                             0,
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

    // For Gaudi2 we are using reduction on SRAM (SRAM size != 0)
    // For Gaudi3 we are using reduction on HBM and must allocate memory on device for FW. (SRAM size == 0)
    if (GCFG_FW_IMB_SIZE.value() && GCFG_HCL_SRAM_SIZE_RESERVED_FOR_HCL.value() == 0)
    {
        uint64_t sizeOfFwBuffers = GCFG_FW_IMB_SIZE.value();
        VERIFY(synSuccess == synDeviceMalloc(deviceId, sizeOfFwBuffers, 0, 0, &m_fwBaseAddr),
               "Failed to allocate device memory for FW");
    }
}

IntermediateBufferContainer::~IntermediateBufferContainer()
{
    for (unsigned poolSizeIndex = 0; poolSizeIndex < m_bufferContainerParams.size(); poolSizeIndex++)
    {
        synDeviceFree(m_deviceId, m_bufferContainerParams[poolSizeIndex].allBufferBaseAddr, 0);
    }

    if (GCFG_FW_IMB_SIZE.value())
    {
        synDeviceFree(m_deviceId, m_fwBaseAddr, 0);
    }
}

void IntermediateBufferContainer::generatePoolParams(unsigned                           sliceSize,
                                                     const std::vector<e_devicePoolID>& pools,
                                                     BufferContainerParams&             m_bufferContainerParams)
{
    m_bufferContainerParams.sliceSize        = sliceSize;
    m_bufferContainerParams.countOfSIB       = hcl::IntermediateBufferContainer::getSIBCount(pools);
    m_bufferContainerParams.sizeOfSIB        = sliceSize * m_bufferContainerParams.countOfSIB;
    m_bufferContainerParams.sizeOfAllBuffers = m_bufferContainerParams.sizeOfSIB * m_numberOfStreams;

    // Make sure each pool number is divisible by its factor (for RR granularity)
    VERIFY(verifySIBPoolSizes(pools));
}

uint32_t hcl::IntermediateBufferContainer::getSizeOfAllBuffers(unsigned poolSizeIndex) const
{
    return m_bufferContainerParams[poolSizeIndex].sizeOfAllBuffers;
}

uint64_t hcl::IntermediateBufferContainer::getBufferSize() const
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
    return GCFG_FW_IMB_SIZE.value();
}

unsigned IntermediateBufferContainer::getSIBCount(const std::vector<e_devicePoolID>& pools)
{
    unsigned count = 0;
    for (const auto& pool : pools)
    {
        count += hcl::IntermediateBuffersAmount::getBufferCount(pool);
    }
    return count;
}

std::vector<unsigned> IntermediateBufferContainer::getSIBVector()
{
    std::vector<unsigned> SIBVector = {};
    for (int i = m_firstPool; i < m_lastPool + 1; i++)
    {
        SIBVector.push_back(hcl::IntermediateBuffersAmount::getBufferCount(static_cast<e_devicePoolID>(i)));
    }
    return SIBVector;
}

bool IntermediateBufferContainer::verifySIBPoolSizes(const std::vector<e_devicePoolID>& pools)
{
    for (const e_devicePoolID& pool : pools)
    {
        switch (pool)
        {
            case REDUCE_RR_POOL:
                if ((hcl::IntermediateBuffersAmount::getBufferCount(pool) % DEFAULT_FACTOR) != 0)
                {
                    return false;
                }
                break;
            case SCALEUP_RR_AND_ALL2ALL_POOL:
                if ((hcl::IntermediateBuffersAmount::getBufferCount(pool) % RR_SCALEUP_FACTOR) != 0)
                {
                    return false;
                }
                break;
            case SCALEOUT_RR_POOL:
            case SCALEOUT_GDR_POOL:
                if ((hcl::IntermediateBuffersAmount::getBufferCount(pool) % RR_SCALEOUT_FACTOR) != 0)
                {
                    return false;
                }
                break;
            default:
                VERIFY(false, "Illegal poolIdx={}", pool);
                break;
        }
    }
    return true;
}