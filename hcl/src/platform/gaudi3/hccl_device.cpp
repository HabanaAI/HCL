#include "platform/gaudi3/hccl_device.h"
#include "platform/gaudi3/hcl_collective_routines.h"  // for HclCollect...
#include "platform/gen2_arch_common/wqe_tracker.h"    // for WqeTracker

hcclResult_t hccl_gaudi3_t::init_device(uint8_t apiId)
{
    // export HBM for GDR if required
    device_->exportHBMMR();

    FOR_I(device_->getHal()->getMaxStreams())
    {
        collectives_.push_back(new HclCollectiveRoutinesGaudi3((HclDeviceGaudi3*)device_, i, new WqeTracker()));
    }

    device_->getScalManager().initSimb(device_, apiId);

    LOG_HCL_DEBUG(HCL, "G3 device created");

    return hcclSuccess;
}
