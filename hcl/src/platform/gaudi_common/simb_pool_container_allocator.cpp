#include "platform/gaudi_common/simb_pool_container_allocator.h"  // for SimbPoolContainerAllocatorGaudiCommon
#include "platform/gaudi_common/gaudi_consts.h"                   // for SYN_VALID_DEVICE_ID
#include "synapse_api.h"                                          // for synDeviceFree, synDeviceMalloc
#include "hcl_utils.h"                                            // for VERIFY

SimbPoolContainerAllocatorGaudiCommon::SimbPoolContainerAllocatorGaudiCommon(uint64_t numberOfStreams)
: SimbPoolContainerAllocator(numberOfStreams)
{
}

bool SimbPoolContainerAllocatorGaudiCommon::allocateDeviceMemory(const uint64_t size, uint64_t* address)
{
    if (synDeviceMalloc(SYN_VALID_DEVICE_ID, size, 0, 0, address) != synSuccess)
    {
        LOG_HCL_ERR(HCL, "Failed on synDeviceMalloc to allocate memory");
        return false;
    }

    return true;
}

void SimbPoolContainerAllocatorGaudiCommon::freeDeviceMemory(uint64_t address)
{
    VERIFY(synDeviceFree(SYN_VALID_DEVICE_ID, address, 0) == synSuccess, "Failed on synDeviceFree to free memory");
}

void SimbPoolContainerAllocatorGaudiCommon::allocateFwIntermediateBuffer()
{
    // For Gaudi2 we are using reduction on SRAM (SRAM size != 0)
    // For Gaudi3 we are using reduction on HBM and must allocate memory on device for FW. (SRAM size == 0)
    if (GCFG_FW_IMB_SIZE.value() && GCFG_HCL_SRAM_SIZE_RESERVED_FOR_HCL.value() == 0)
    {
        m_fwImbSize = GCFG_FW_IMB_SIZE.value();
        VERIFY(allocateDeviceMemory(m_fwImbSize, &m_fwBaseAddr), "Failed to allocate device memory for FW");
    }
}