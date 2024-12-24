#pragma once

#include "platform/gen2_arch_common/device_buffer_manager.h"
#include <cstdint>
#include <vector>
#include <map>

constexpr unsigned MAX_NUM_POOLS = 20;
struct IntermediateBuffersAmount
{
    static constexpr std::array<std::pair<e_devicePoolID, unsigned>, MAX_NUM_POOLS> buffersArr = {
        {{SCALEOUT_POOL, 40},
         {REDUCE_POOL, 8},  // only 4 needed, but we use 8 for granularity
         {SCALEUP_AND_ALL2ALL_POOL, 104},
         {SCALEOUT_GDR_POOL, 40}}};

    static int getBufferCount(e_devicePoolID key)
    {
        for (const auto& pair : buffersArr)
        {
            if (pair.first == key)
            {
                return pair.second;
            }
        }
        return -1;  // not found
    }
};

struct BufferContainerParams
{
    uint64_t allBufferBaseAddr = 0;
    unsigned sliceSize         = 0;
    uint64_t sizeOfAllBuffers  = 0;
    uint64_t sizeOfSIB         = 0;
    uint64_t countOfSIB        = 0;
};

/*
    IntermediateBufferContainer allocated 2 ranges in HBM to be used for IMBs.
    Each range is divided to 3 smaller ranges to be used per stream (managed by DeviceBufferManager).
    DeviceBufferManager can contain many pool types (REDUCE_POOL/SCALEUP_AND_ALL2ALL_POOL/SCALEOUT_POOL) and
    different pool sizes. SCALEOUT_POOL - 1M buffers. REDUCE_POOL/SCALEUP_AND_ALL2ALL_POOL - 512k buffers.
*/
class IntermediateBufferContainer
{
public:
    IntermediateBufferContainer(uint64_t numberOfStreams);
    virtual ~IntermediateBufferContainer() = default;

    void init();
    void destroy();

    virtual bool allocateDeviceMemory(const uint64_t size, uint64_t* buffer) = 0;
    virtual void freeDeviceMemory(uint64_t buffer)                           = 0;
    virtual void allocateFwIntermediateBuffer()                              = 0;

    uint64_t              getBufferSize() const;
    DeviceBufferManager&  getSIB(uint32_t streamIndex);
    uint64_t              getAllBufferBaseAddr(unsigned poolSizeIndex);
    uint64_t              getFwBaseAddr();
    unsigned              getSliceSize(unsigned poolSizeIndex) const;
    unsigned              getFwSliceSize();
    static unsigned       getSIBCount(const std::vector<e_devicePoolID>& pools);
    std::vector<unsigned> getSIBVector();
    /**
     * @brief Get the size of all buffer pools allocated by HCL
     *
     * @return uint32_t size of all buffers
     */
    uint32_t getSizeOfAllBuffers(unsigned poolSizeIndex) const;
    bool     verifySIBPoolSizes(const std::vector<e_devicePoolID>& pools);

    inline e_devicePoolID getFirstPool() { return m_firstPool; };
    inline e_devicePoolID getLastPool() { return m_lastPool; };

    void generatePoolParams(unsigned                           sliceSize,
                            const std::vector<e_devicePoolID>& pools,
                            BufferContainerParams&             bufferContainerParams);

protected:
    uint64_t m_fwBaseAddr = 0;
    uint64_t m_fwImbSize  = 0;

private:
    std::vector<DeviceBufferManager>                      m_sibBuffers;
    uint64_t                                              m_numberOfStreams = 0;
    std::array<BufferContainerParams, MAX_NUM_POOL_SIZES> m_bufferContainerParams;
    e_devicePoolID                                        m_firstPool = NO_POOL;
    e_devicePoolID                                        m_lastPool  = NO_POOL;
    uint64_t                                              m_imbSize;
};
