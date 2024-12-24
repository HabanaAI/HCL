#pragma once

#include "platform/gen2_arch_common/hccl_device.h"  // for hccl_device_t
#include "platform/gaudi2/hcl_device.h"             // for HclDeviceGaudi2
#include "synapse_common_types.h"                   // for synDeviceType

class hccl_gaudi2_t : public hccl_device_t
{
public:
    hccl_gaudi2_t(class HclDeviceGaudi2* _device) : hccl_device_t((HclDeviceGen2Arch*)_device, synDeviceGaudi2) {}
    virtual hcclResult_t init_device(uint8_t apiId) override;

protected:
    virtual uint32_t stream_id(void* streamHandle) override;
};