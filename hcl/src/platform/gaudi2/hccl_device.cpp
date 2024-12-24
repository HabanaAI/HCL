#include "platform/gaudi2/hccl_device.h"
#include "platform/gaudi2/hcl_collective_routines.h"  // for HclCollect...
#include "platform/gaudi2/wqe_tracker.h"              // for WqeTrackerGaudi2

hcclResult_t hccl_gaudi2_t::init_device(uint8_t apiId)
{
    FOR_I(device_->getHal().getMaxStreams())
    {
        collectives_.push_back(new HclCollectiveRoutinesGaudi2((HclDeviceGaudi2*)device_, i, new WqeTrackerGaudi2()));
    }

    device_->getScalManager().initGlobalContext(device_, apiId);

    LOG_HCL_DEBUG(HCL, "G2 device created");

    return hcclSuccess;
}

uint32_t hccl_gaudi2_t::stream_id(void* streamHandle)
{
    if (auto handle = synStreamGetHclStreamHandle((synStreamHandle)streamHandle))
    {
        return hcl::getStreamID(handle);
    }

    return 0;
}