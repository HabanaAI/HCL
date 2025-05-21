#include "device_simb_pool_manager.h"

#include <cstdint>

#include "hcl_device.h"
#include "hcl_global_conf.h"
#include "hcl_utils.h"        // for VERIFY
#include "hcl_log_manager.h"  // for LOG_*
#include "hcl_math_utils.h"
#include "infra/scal/gen2_arch_common/scal_manager.h"  // for getHBMBaseVAAddress
#include "infra/buffer_handle_generator.h"
#include "platform/gen2_arch_common/simb_pool_manager_base.h"
#include "platform/gen2_arch_common/simb_pool_container_allocator.h"  // for SimbPoolContainerAllocator

DeviceSimbPoolManagerBase::DeviceSimbPoolManagerBase(
    std::array<SimbPoolContainerParamsPerStream, MAX_POOL_CONTAINER_IDX> spcParamsPerStream,
    const std::map<e_devicePoolID, unsigned>&                            sizes)
: SimbPoolManagerBase(spcParamsPerStream, sizes)
{
}

void DeviceSimbPoolManagerBase::init()
{
    unsigned gcfgFactor                       = GCFG_HCL_SCALEOUT_BUFFER_FACTOR.value();
    unsigned poolBase[MAX_POOL_CONTAINER_IDX] = {0, 0};
    for (auto const& sizeEntry : m_poolSizes)
    {
        e_devicePoolID poolIndex = sizeEntry.first;
        m_poolBases.emplace(poolIndex, poolBase[getPoolContainerIndex((e_devicePoolID)poolIndex)]);
        m_creditManagers.emplace(poolIndex,
                                 m_poolSizes.at(poolIndex) / getFactor(static_cast<e_devicePoolID>(poolIndex)));
        poolBase[getPoolContainerIndex((e_devicePoolID)poolIndex)] += m_poolSizes.at(poolIndex);
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

const unsigned DeviceSimbPoolManagerBase::getFactor(const e_devicePoolID poolIdx)
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

e_devicePoolID DeviceSimbPoolManagerBase::fetchPool(const BufferToken& bufferHandle)
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

bool DeviceSimbPoolManagerBase::isPoolAllocated(e_devicePoolID poolIdx)
{
    return m_poolSizes.count(poolIdx) > 0;
}

bool DeviceSimbPoolManagerBase::isSiboPool(e_devicePoolID poolIdx)
{
    return poolIdx == SCALEOUT_POOL || poolIdx == SCALEOUT_POOL_1 || poolIdx == SCALEOUT_ACC_POOL ||
           poolIdx == SCALEUP_AND_ALL2ALL_POOL;
}

uint32_t DeviceSimbPoolManagerBase::getSliceId(e_devicePoolID poolIdx, uint32_t streamId)
{
    unsigned currentCredit      = getCurrentBufferIdx(poolIdx);
    unsigned poolContainerIndex = getPoolContainerIndex(poolIdx);

    uint32_t sliceId = currentCredit * getFactor(poolIdx) + m_poolBases[poolIdx];
    sliceId +=
        (streamId *
         m_spcParamsPerStream[poolContainerIndex].m_simbCountPerStream);  // adds all the other pools for streams > 0

    return sliceId;
}

uint32_t DeviceSimbPoolManagerBase::getCurrentBufferIdx(e_devicePoolID poolIdx)
{
    return m_creditManagers[poolIdx].getCurrentCredit();
}

uint64_t DeviceSimbPoolManagerBase::getBufferBaseAddr(const e_devicePoolID poolIdx) const
{
    unsigned poolContainerIndex = getPoolContainerIndex(poolIdx);
    return m_spcParamsPerStream.at(poolContainerIndex).m_streamBaseAddrInContainer;
}

uint64_t DeviceSimbPoolManagerBase::getStreamBaseAddrInContainer(const unsigned poolContainerIndex) const
{
    return m_spcParamsPerStream.at(poolContainerIndex).m_streamBaseAddrInContainer;
}

unsigned DeviceSimbPoolManagerBase::getPoolBufferSize(const e_devicePoolID poolIdx) const
{
    return m_poolSizes.at(poolIdx) / getFactor(poolIdx);
}

uint64_t DeviceSimbPoolManagerBase::getSingleBufferSize(const e_devicePoolID poolIdx) const
{
    unsigned poolContainerIndex = getPoolContainerIndex(poolIdx);
    return m_spcParamsPerStream.at(poolContainerIndex).m_simbSize;
}

uint64_t DeviceSimbPoolManagerBase::getSimbSize(const unsigned poolContainerIndex) const
{
    return m_spcParamsPerStream.at(poolContainerIndex).m_simbSize;
}

uint64_t DeviceSimbPoolManagerBase::getCurrentBuffer(const e_devicePoolID poolIdx)
{
    const int idx = m_creditManagers[poolIdx].getCurrentCredit();
    if (idx < 0)
    {
        return -1;
    }

    unsigned factor = getFactor(poolIdx);
    return getBufferBaseAddr(poolIdx) + (((idx * factor) + m_poolBases[poolIdx]) * getSingleBufferSize(poolIdx));
}

int64_t DeviceSimbPoolManagerBase::getCurrentTargetValue(const e_devicePoolID               poolIdx,
                                                         [[maybe_unused]] const hcclRedOp_t reduceOp)
{
    return m_creditManagers[poolIdx].getCurrentTargetValue();
}

void DeviceSimbPoolManagerBase::advanceProg(uint64_t currTargetValue)
{
    for (auto& creditManagerEntry : m_creditManagers)
    {
        creditManagerEntry.second.advanceProg(currTargetValue);
    }
}

bool DeviceSimbPoolManagerBase::bufferExpired(e_devicePoolID poolId)
{
    return m_creditManagers[poolId].isCreditExpiring();
}

uint64_t DeviceSimbPoolManagerBase::allocNextBuffer(uint64_t targetValue, const e_devicePoolID poolIdx)
{
    return m_creditManagers[poolIdx].allocNextCredit(targetValue);
}

unsigned DeviceSimbPoolManagerBase::getPoolContainerIndexByAddr(uint64_t address)
{
    unsigned retIndex = 0;
    for (unsigned poolContainerIndex = 0; poolContainerIndex < m_spcParamsPerStream.size(); poolContainerIndex++)
    {
        uint64_t startAddr = m_spcParamsPerStream[poolContainerIndex].m_streamBaseAddrInContainer;
        uint64_t endAddr   = startAddr + m_spcParamsPerStream[poolContainerIndex].m_simbSize *
                                           m_spcParamsPerStream[poolContainerIndex].m_simbCountPerStream;
        if (m_spcParamsPerStream[poolContainerIndex].m_streamBaseAddrInContainer <= address && (address < endAddr))
        {
            retIndex = poolContainerIndex;
            break;
        }
    }

    return retIndex;
}

SimbPoolContainerParamsPerStream&
DeviceSimbPoolManagerBase::getPoolContainerParamsPerStream(const unsigned poolContainerIndex)
{
    return m_spcParamsPerStream.at(poolContainerIndex);
}
