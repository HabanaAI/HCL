#include "platform/gaudi3/device_simb_pool_manager.h"
#include "hcl_global_conf.h"
#include "hcl_utils.h"

DeviceSimbPoolManagerGaudi3::DeviceSimbPoolManagerGaudi3(
    std::array<SimbPoolContainerParamsPerStream, MAX_POOL_CONTAINER_IDX> spcParamsPerStream,
    const std::map<e_devicePoolID, unsigned>&                            sizes)
: DeviceSimbPoolManagerBase(spcParamsPerStream, sizes)
{
    init();
}

unsigned DeviceSimbPoolManagerGaudi3::getPoolContainerIndex(const e_devicePoolID poolIdx) const
{
    if ((poolIdx == SCALEOUT_POOL && GCFG_HCL_RS_SO_RECV_CONT_REDUCTION.value()))
    {
        return SIBO_STANDARD_SIMB_SIZE;
    }

    switch (poolIdx)
    {
        case SCALEOUT_POOL_0:  // FALLTHROUGH
        case SCALEOUT_ACC_POOL:
            return SIBO_DOUBLE_SIMB_SIZE;
        case SCALEOUT_POOL_1:  // FALLTHROUGH
        case SCALEUP_AND_ALL2ALL_POOL:
            return SIBO_STANDARD_SIMB_SIZE;

        case REDUCE_POOL:  // FALLTHROUGH
        case SCALEOUT_GDR_POOL:
            return NON_SIBO_STANDARD_SIMB_SIZE;

        case PRIMITIVE_POOL:
            return NON_SIBO_DOUBLE_SIMB_SIZE;

        case NO_POOL:  // FALLTHROUGH
        default:
            VERIFY(false, "Unsupported poolIdx");
            break;
    }
    return INVALID_POOL_CONTAINER_IDX;
}