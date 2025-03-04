#pragma once

#include "platform/gen2_arch_common/device_buffer_manager.h"
#include <cstdint>
#include <vector>
#include <map>
#include "hcl_global_conf.h"
#include "hcl_log_manager.h"  // unlikely

constexpr unsigned MAX_NUM_POOLS              = 20;
constexpr unsigned MAX_RANKS_IN_SCALEUP_GROUP = 8;
constexpr unsigned SCALEOUT_POOL_SIZE         = 40;
constexpr unsigned REDUCE_POOL_SIZE           = 8;
constexpr unsigned SCALEOUT_GDR_POOL_SIZE     = 40;
constexpr unsigned PRIMITIVE_POOL_SIZE        = 8;
static int         s_scaleUpPoolSize          = -1;
static int         s_scaleoutTempPoolSize     = -1;

struct IntermediateBuffersAmount
{
    static int getBufferCount(e_devicePoolID pool)
    {
        switch (pool)
        {
            case SCALEOUT_POOL:
                return SCALEOUT_POOL_SIZE;
                break;
            case REDUCE_POOL:
                return REDUCE_POOL_SIZE;
                break;
            case SCALEUP_AND_ALL2ALL_POOL:
                if (unlikely(s_scaleUpPoolSize == -1))
                {
                    s_scaleUpPoolSize = GCFG_HCL_SCALEUP_SIMB_COUNT.value() * MAX_RANKS_IN_SCALEUP_GROUP;
                }
                return s_scaleUpPoolSize;
                break;
            case SCALEOUT_GDR_POOL:
                return SCALEOUT_GDR_POOL_SIZE;
                break;
            case PRIMITIVE_POOL:
                return PRIMITIVE_POOL_SIZE;
            case SCALEOUT_POOL_1:
                return SCALEOUT_POOL_SIZE;
                break;
            case SCALEOUT_ACC_POOL:
                if (s_scaleoutTempPoolSize == -1)
                {
                    // need temp buffer for each scaleout buffer:
                    // (number of scaleout pool) * (number of buffers per pool)
                    // ** size should be round up to be divisible  by 8
                    s_scaleoutTempPoolSize =
                        (RS_CONT_REDUC_SO_POOL_AMOUNT * (SCALEOUT_POOL_SIZE / GCFG_HCL_SCALEOUT_BUFFER_FACTOR.value()) +
                         7) &
                        ~7;
                }
                return s_scaleoutTempPoolSize;
                break;
            default:
                LOG_ERR(HCL, "Pool {} is not supported", pool);
                return -1;
        }
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

    uint64_t             getBufferSize() const;
    DeviceBufferManager& getSIB(uint32_t streamIndex);
    uint64_t             getAllBufferBaseAddr(unsigned poolSizeIndex);
    uint64_t             getFwBaseAddr();
    unsigned             getSliceSize(unsigned poolSizeIndex) const;
    unsigned             getFwSliceSize();
    static unsigned      getSIBCount(const std::vector<e_devicePoolID>& pools);
    std::map<e_devicePoolID, unsigned>
    getSIBMap(std::array<std::vector<e_devicePoolID>, MAX_NUM_POOL_SIZES>& poolTypes);
    /**
     * @brief Get the size of all buffer pools allocated by HCL
     *
     * @return uint32_t size of all buffers
     */
    uint32_t getSizeOfAllBuffers(unsigned poolSizeIndex) const;
    bool     verifySIBPoolSizes(const std::vector<e_devicePoolID>& pools);

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
    uint64_t                                              m_imbSize;
};
