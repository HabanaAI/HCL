#include "platform/gaudi2/device_simb_pool_manager.h"
#include "hcl_global_conf.h"
#include "hcl_utils.h"  // for VERIFY

DeviceSimbPoolManagerGaudi2::DeviceSimbPoolManagerGaudi2(
    std::array<SimbPoolContainerParamsPerStream, MAX_POOL_CONTAINER_IDX> spcParamsPerStream,
    const std::map<e_devicePoolID, unsigned>&                            sizes)
: DeviceSimbPoolManagerBase(spcParamsPerStream, sizes)
{
    init();
}

unsigned DeviceSimbPoolManagerGaudi2::getPoolContainerIndex(const e_devicePoolID poolIdx) const
{
    if ((poolIdx == SCALEOUT_POOL && !GCFG_HCL_RS_SO_RECV_CONT_REDUCTION.value()) || poolIdx == SCALEOUT_ACC_POOL ||
        poolIdx == PRIMITIVE_POOL)
    {
        return SIBO_DOUBLE_SIMB_SIZE;
    }
    else if (poolIdx == NO_POOL)
    {
        VERIFY(false, "Unsupported poolIdx");
    }

    return SIBO_STANDARD_SIMB_SIZE;
}