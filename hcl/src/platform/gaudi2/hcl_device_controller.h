#pragma once
#include "platform/gen2_arch_common/hcl_device_controller.h"  // for HclDeviceControllerGen2Arch

class HclDeviceControllerGaudi2 : public HclDeviceControllerGen2Arch
{
public:
    HclDeviceControllerGaudi2(const int fd, const unsigned numOfStreams);
    virtual ~HclDeviceControllerGaudi2()                                   = default;
    HclDeviceControllerGaudi2(const HclDeviceControllerGaudi2&)            = delete;
    HclDeviceControllerGaudi2& operator=(const HclDeviceControllerGaudi2&) = delete;
};