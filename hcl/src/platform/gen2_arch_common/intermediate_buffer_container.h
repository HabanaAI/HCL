#pragma once

#include "platform/gen2_arch_common/device_buffer_manager.h"
#include <cstdint>
#include <vector>
#include <map>

namespace hcl
{
constexpr unsigned MAX_NUM_POOLS = 20;
struct IntermediateBuffersAmount
{
    static constexpr std::array<std::pair<e_devicePoolID, unsigned>, MAX_NUM_POOLS> buffersArr = {
        {{SCALEOUT_RR_POOL, 40},
         {REDUCE_RR_POOL, 8},  // only 4 needed, but we use 8 for granularity
         {SCALEUP_RR_AND_ALL2ALL_POOL, 104},
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
    unsigned sliceSize         = 0;  // TODO make const
    uint64_t sizeOfAllBuffers  = 0;
    uint64_t sizeOfSIB         = 0;
    uint64_t countOfSIB        = 0;
};

/*
    IntermediateBufferContainer allocated 2 ranges in HBM to be used for IMBs.
    Each range is divided to 3 smaller ranges to be used per stream (managed by DeviceBufferManager).
    DeviceBufferManager can contain many pool types (REDUCE_RR_POOL/SCALEUP_RR_AND_ALL2ALL_POOL/SCALEOUT_RR_POOL) and
    different pool sizes. SCALEOUT_RR_POOL - 1M buffers. REDUCE_RR_POOL/SCALEUP_RR_AND_ALL2ALL_POOL - 512k buffers.
*/
class IntermediateBufferContainer
{
public:
    explicit IntermediateBufferContainer(uint32_t deviceId, uint32_t numberOfStreams);
    ~IntermediateBufferContainer();
    IntermediateBufferContainer(IntermediateBufferContainer&&)      = delete;
    IntermediateBufferContainer(const IntermediateBufferContainer&) = delete;
    IntermediateBufferContainer& operator=(IntermediateBufferContainer&&) = delete;
    IntermediateBufferContainer& operator=(const IntermediateBufferContainer&) = delete;

    uint64_t              getBufferSize() const;
    DeviceBufferManager&  getSIB(uint32_t streamIndex);                  // TODO make const
    uint64_t              getAllBufferBaseAddr(unsigned poolSizeIndex);  // TODO make const
    uint64_t              getFwBaseAddr();         // TODO make const
    unsigned              getSliceSize(unsigned poolSizeIndex) const;
    unsigned              getFwSliceSize();        // TODO make const
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
                            BufferContainerParams&             m_bufferContainerParams);

private:
    std::vector<DeviceBufferManager>                      m_sibBuffers;
    uint32_t                         m_deviceId          = 0;  // TODO make const
    uint64_t                         m_fwBaseAddr        = 0;
    uint32_t                                              m_numberOfStreams   = 0;  // TODO make const
    std::array<BufferContainerParams, MAX_NUM_POOL_SIZES> m_bufferContainerParams;
    e_devicePoolID                                        m_firstPool = NO_POOL;
    e_devicePoolID                                        m_lastPool  = NO_POOL;
    uint64_t                                              m_imbSize;
};
}  // namespace hcl
