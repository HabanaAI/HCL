#pragma once

#include "platform/gen2_arch_common/hccl_device.h"  // for hccl_device_t
#include "platform/gaudi3/hcl_device.h"             // for HclDeviceGaudi3
#include "synapse_common_types.h"                   // for synDeviceType

class hccl_gaudi3_t : public hccl_device_t
{
public:
    hccl_gaudi3_t(class HclDeviceGaudi3* _device) : hccl_device_t((HclDeviceGen2Arch*)_device, synDeviceGaudi3) {}
    virtual hcclResult_t init_device(uint8_t apiId) override;

protected:
    virtual uint32_t stream_id(void* streamHandle) override;
};