#include "device_buffer_manager.h"

#include <cstdint>

#include "hcl_device.h"
#include "hcl_global_conf.h"
#include "hcl_utils.h"        // for VERIFY
#include "hcl_log_manager.h"  // for LOG_*
#include "hcl_math_utils.h"
#include "infra/scal/gen2_arch_common/scal_manager.h"  // for getHBMBaseVAAddress
#include "infra/buffer_handle_generator.h"
#include "platform/gen2_arch_common/buffer_manager_base.h"
#include "platform/gen2_arch_common/intermediate_buffer_container.h"  // for IntermediateBufferContainer

DeviceBufferManager::DeviceBufferManager(std::array<BufferParams, MAX_NUM_POOL_SIZES> bufferParams,
                                         const std::map<e_devicePoolID, unsigned>&    sizes)
: BufferManagerBase(bufferParams, sizes)
{
    unsigned gcfgFactor                   = GCFG_HCL_SCALEOUT_BUFFER_FACTOR.value();
    unsigned poolBase[MAX_NUM_POOL_SIZES] = {0, 0};
    for (auto const& sizeEntry : m_poolSizes)
    {
        e_devicePoolID poolIndex = sizeEntry.first;
        m_poolBases.emplace(poolIndex, poolBase[getPoolSizeIndex((e_devicePoolID)poolIndex)]);
        m_creditManagers.emplace(poolIndex,
                                 m_poolSizes.at(poolIndex) / getFactor(static_cast<e_devicePoolID>(poolIndex)));
        poolBase[getPoolSizeIndex((e_devicePoolID)poolIndex)] += m_poolSizes.at(poolIndex);
    }
    VERIFY(gcfgFactor <= MAX_SCALEOUT_FACTOR,
           "HCL_SCALEOUT_BUFFER_FACTOR({}) is expected to be <= {}",
           gcfgFactor,
           MAX_SCALEOUT_FACTOR);
    VERIFY(gcfgFactor > MIN_SCALEOUT_FACTOR,
           "HCL_SCALEOUT_BUFFER_FACTOR({}) is expected to be > {}",
           gcfgFactor,
           MIN_SCALEOUT_FACTOR);
}

const unsigned DeviceBufferManager::getFactor(const e_devicePoolID poolIdx)
{
    unsigned factor = s_defaultFactor;
    if (poolIdx == SCALEUP_AND_ALL2ALL_POOL)
    {
        factor = s_scaleupFactor;
    }
    else if ((poolIdx == SCALEOUT_POOL) ||
             ((poolIdx == SCALEOUT_POOL_1) && (GCFG_HCL_RS_SO_RECV_CONT_REDUCTION.value())))
    {
        factor = GCFG_HCL_SCALEOUT_BUFFER_FACTOR.value();
    }
    else if (poolIdx == SCALEOUT_ACC_POOL)
    {
        factor = RS_CONT_REDUC_SO_POOL_AMOUNT;
    }

    return factor;
}

e_devicePoolID DeviceBufferManager::fetchPool(const BufferToken& bufferHandle)
{
    e_devicePoolID ret;
    switch (bufferHandle.bufferType)
    {
        case TEMP_BUFFER:
            ret = SCALEUP_AND_ALL2ALL_POOL;
            break;

        case STATIC_BUFFER:
            ret = PRIMITIVE_POOL;
            break;

        default:
            ret = NO_POOL;
            break;
    }
    return ret;
}

bool DeviceBufferManager::isPoolAllocated(e_devicePoolID poolIdx)
{
    return m_poolSizes.count(poolIdx) > 0;
}

bool DeviceBufferManager::isSiboPool(e_devicePoolID poolIdx)
{
    return poolIdx == SCALEOUT_POOL || poolIdx == SCALEOUT_POOL_1 || poolIdx == SCALEOUT_ACC_POOL ||
           poolIdx == SCALEUP_AND_ALL2ALL_POOL;
}

uint32_t DeviceBufferManager::getSliceId(e_devicePoolID poolIdx, uint32_t streamId)
{
    unsigned currentCredit = getCurrentBufferIdx(poolIdx);
    unsigned poolSizeIndex = getPoolSizeIndex(poolIdx);

    uint32_t sliceId = currentCredit * getFactor(poolIdx) + m_poolBases[poolIdx];
    sliceId +=
        (streamId * m_bufferParams[poolSizeIndex].m_totalPoolsAmount);  // adds all the other pools for streams > 0

    return sliceId;
}

uint32_t DeviceBufferManager::getCurrentBufferIdx(e_devicePoolID poolIdx)
{
    return m_creditManagers[poolIdx].getCurrentCredit();
}

uint64_t DeviceBufferManager::getBufferBaseAddr(const e_devicePoolID poolIdx) const
{
    unsigned poolSizeIndex = getPoolSizeIndex(poolIdx);
    return m_bufferParams.at(poolSizeIndex).m_bufferBaseAddr;
}

uint64_t DeviceBufferManager::getBufferBaseAddr(const unsigned index) const
{
    return m_bufferParams.at(index).m_bufferBaseAddr;
}

unsigned DeviceBufferManager::getPoolBufferSize(const e_devicePoolID poolIdx) const
{
    return m_poolSizes.at(poolIdx) / getFactor(poolIdx);
}

uint64_t DeviceBufferManager::getSingleBufferSize(const e_devicePoolID poolIdx) const
{
    unsigned poolSizeIndex = getPoolSizeIndex(poolIdx);
    return m_bufferParams.at(poolSizeIndex).m_singleBufferSize;
}

uint64_t DeviceBufferManager::getSingleBufferSize(const unsigned index) const
{
    return m_bufferParams.at(index).m_singleBufferSize;
}

uint64_t DeviceBufferManager::getCurrentBuffer(const e_devicePoolID poolIdx)
{
    const int idx = m_creditManagers[poolIdx].getCurrentCredit();
    if (idx < 0)
    {
        return -1;
    }

    unsigned factor = getFactor(poolIdx);
    return getBufferBaseAddr(poolIdx) + (((idx * factor) + m_poolBases[poolIdx]) * getSingleBufferSize(poolIdx));
}

int64_t DeviceBufferManager::getCurrentTargetValue(const e_devicePoolID               poolIdx,
                                                   [[maybe_unused]] const hcclRedOp_t reduceOp)
{
    return m_creditManagers[poolIdx].getCurrentTargetValue();
}

void DeviceBufferManager::advanceProg(uint64_t currTargetValue)
{
    for (auto& creditManagerEntry : m_creditManagers)
    {
        creditManagerEntry.second.advanceProg(currTargetValue);
    }
}

bool DeviceBufferManager::bufferExpired(e_devicePoolID poolId)
{
    return m_creditManagers[poolId].isCreditExpiring();
}

uint64_t DeviceBufferManager::allocNextBuffer(uint64_t targetValue, const e_devicePoolID poolIdx)
{
    return m_creditManagers[poolIdx].allocNextCredit(targetValue);
}

unsigned DeviceBufferManager::getPoolSizeIndex(const e_devicePoolID poolIdx)
{
    if ((poolIdx == SCALEOUT_POOL && !GCFG_HCL_RS_SO_RECV_CONT_REDUCTION.value()) || poolIdx == SCALEOUT_ACC_POOL ||
        poolIdx == PRIMITIVE_POOL)
    {
        return DOUBLE_SLICE_SIZE_POOL_IDX;
    }
    else if (poolIdx == NO_POOL)
    {
        VERIFY(false, "Unsupported poolIdx");
    }

    return STANDARD_SLICE_SIZE_POOL_IDX;
}

unsigned DeviceBufferManager::getPoolSizeIndexByAddr(uint64_t address)
{
    unsigned rcPoolSizeIndex = 0;
    for (unsigned poolSizeIndex = 0; poolSizeIndex < m_bufferParams.size(); poolSizeIndex++)
    {
        uint64_t startAddr = m_bufferParams[poolSizeIndex].m_bufferBaseAddr;
        uint64_t endAddr   = startAddr + m_bufferParams[poolSizeIndex].m_singleBufferSize *
                                           m_bufferParams[poolSizeIndex].m_totalPoolsAmount;
        if (m_bufferParams[poolSizeIndex].m_bufferBaseAddr <= address && (address < endAddr))
        {
            rcPoolSizeIndex = poolSizeIndex;
            break;
        }
    }

    return rcPoolSizeIndex;
}

uint64_t DeviceBufferManager::getBufferAmountInPool(unsigned poolId)
{
    return m_bufferParams.at(poolId).m_totalPoolsAmount;
}
