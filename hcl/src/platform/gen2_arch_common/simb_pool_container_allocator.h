#pragma once

#include "platform/gen2_arch_common/device_simb_pool_manager.h"
#include <cstdint>
#include <vector>
#include <map>
#include "hcl_global_conf.h"
#include "hcl_log_manager.h"  // unlikely

/*
+--------------------------+-------------+---------------+-------+---------------+---------------+
|          Pool           | Granularity |     Count      | Size  | SIBO (gaudi3) | SIBO (gaudi2) |
+--------------------------+-------------+---------------+-------+---------------+---------------+
| SCALEOUT_POOL_0         |      8      |       5        |  1/2  |       +       |       +       |
| REDUCE_POOL             |      1      |       8        |   1   |       -       |       +       |
| PRIMITIVE_POOL          |      1      |       8        |  0/2  |       -       |       +       |
| SCALEUP_AND_ALL2ALL_POOL|      8      |    13 /20      |   1   |       +       |       +       |
| SCALEOUT_GDR_POOL       |      1      |      40        |  0/1  |       -       |       +       |
| SCALEOUT_POOL_1         |      8      |       5        |  0/1  |       +       |       +       |
| SCALEOUT_ACC_POOL       |      2      | 5 (actual 8)   |  0/2  |       +       |       +       |
+--------------------------+-------------+----------------+------+---------------+---------------+
*/

constexpr unsigned MAX_NUM_POOLS              = 20;
constexpr unsigned MAX_RANKS_IN_SCALEUP_GROUP = 8;
constexpr unsigned SCALEOUT_POOL_SIZE         = 40;
constexpr unsigned REDUCE_POOL_SIZE           = 8;
constexpr unsigned SCALEOUT_GDR_POOL_SIZE     = 40;
constexpr unsigned PRIMITIVE_POOL_SIZE        = 8;
static int         s_scaleUpPoolSize          = -1;
static int         s_scaleoutTempPoolSize     = -1;

struct SimbCountInPool
{
    static int getSimbCount(e_devicePoolID pool)
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

struct PoolContainerParams
{
    uint64_t containerBaseAddr        = 0;
    unsigned simbSize                 = 0;
    uint64_t simbCountPerStream       = 0;  // sums of all pools
    uint64_t sizeOfContainerPerStream = 0;
    uint64_t sizeOfContainer          = 0;
    ;
};

/*
    SimbPoolContainerManager allocated 2 ranges in HBM to be used for IMBs.
    Each range is divided to 3 smaller ranges to be used per stream (managed by DeviceSimbPoolManager).
    DeviceSimbPoolManager can contain many pool types (REDUCE_POOL/SCALEUP_AND_ALL2ALL_POOL/SCALEOUT_POOL) and
    different pool sizes. SCALEOUT_POOL - 1M buffers. REDUCE_POOL/SCALEUP_AND_ALL2ALL_POOL - 512k buffers.
*/
class SimbPoolContainerAllocator
{
public:
    SimbPoolContainerAllocator(uint64_t numberOfStreams);
    virtual ~SimbPoolContainerAllocator() = default;

    virtual void init() = 0;
    void         destroy();

    virtual bool allocateDeviceMemory(const uint64_t size, uint64_t* buffer) = 0;
    virtual void freeDeviceMemory(uint64_t buffer)                           = 0;
    virtual void allocateFwIntermediateBuffer()                              = 0;

    uint64_t                   getBufferSize() const;
    DeviceSimbPoolManagerBase& getDeviceSimbPoolManager(uint32_t streamIndex);
    uint64_t                   getPoolContainerBaseAddr(unsigned poolContainerIndex);
    unsigned                   getSimbSize(unsigned poolContainerIndex) const;
    static unsigned            getTotalSimbCount(const std::vector<e_devicePoolID>& pools);
    uint64_t                   getFwBaseAddr();
    unsigned                   getFwSliceSize();

    std::map<e_devicePoolID, unsigned>
    getSIBMap(std::array<std::vector<e_devicePoolID>, MAX_POOL_CONTAINER_IDX>& poolTypes);
    /**
     * @brief Get the size of all buffer pools allocated by HCL
     *
     * @return uint32_t size of all buffers
     */
    uint32_t getSizeOfAllBuffers(unsigned poolContainerIndex) const;
    bool     verifySIBPoolSizes(const std::vector<e_devicePoolID>& pools);

    void generateContainerParams(unsigned                           simbSize,
                                 const std::vector<e_devicePoolID>& pools,
                                 PoolContainerParams&               poolContainerParams);

    // It doesn't matter which manager we take the index from, they are all the same in this case
    unsigned getPoolContainerIndex(const e_devicePoolID poolIdx)
    {
        return m_deviceSimbPoolManagers[0]->getPoolContainerIndex(poolIdx);
    };

protected:
    uint64_t                                                m_fwBaseAddr = 0;
    uint64_t                                                m_fwImbSize  = 0;
    std::vector<std::shared_ptr<DeviceSimbPoolManagerBase>> m_deviceSimbPoolManagers;
    uint64_t                                                m_numberOfStreams = 0;
    std::array<PoolContainerParams, MAX_POOL_CONTAINER_IDX> m_poolContainerParams;
    uint64_t                                                m_imbSize;
};
