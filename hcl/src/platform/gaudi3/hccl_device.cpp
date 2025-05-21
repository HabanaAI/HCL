#include "platform/gaudi3/hccl_device.h"
#include "platform/gaudi3/hcl_collective_routines.h"  // for HclCollect...
#include "platform/gen2_arch_common/wqe_tracker.h"    // for WqeTracker
#include "platform/gaudi3/hcl_device_controller.h"    // for HclDeviceControllerGaudi3

hcclResult_t hccl_gaudi3_t::init_device(uint8_t apiId)
{
    FOR_I(device_->getHal().getMaxArchStreams())
    {
        collectives_.push_back(new HclCollectiveRoutinesGaudi3((HclDeviceGaudi3*)device_, i, new WqeTracker()));
    }

    auto controller = static_cast<HclDeviceControllerGaudi3*>(&device_->m_deviceController);
    controller->clearSimb(device_, apiId);

    LOG_HCL_DEBUG(HCL, "G3 device created");

    return hcclSuccess;
}

uint32_t hccl_gaudi3_t::stream_id(void* streamHandle)
{
    if (auto handle = synStreamGetHclStreamHandle((synStreamHandle)streamHandle))
    {
        return hcl::getStreamID(handle);
    }

    return 0;
}
