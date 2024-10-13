#pragma once
#include "platform/gen2_arch_common/hcl_device_controller.h"  // for HclDeviceControllerGen2Arch

class HclDeviceControllerGaudi3 : public HclDeviceControllerGen2Arch
{
public:
    HclDeviceControllerGaudi3(const int fd, const unsigned numOfStreams);
    virtual ~HclDeviceControllerGaudi3()                                   = default;
    HclDeviceControllerGaudi3(const HclDeviceControllerGaudi3&)            = delete;
    HclDeviceControllerGaudi3& operator=(const HclDeviceControllerGaudi3&) = delete;
};