#pragma once
#include "platform/gen2_arch_common/hcl_device_controller.h"  // for HclDeviceControllerGen2Arch

class HclDeviceControllerGaudi3 : public HclDeviceControllerGen2Arch
{
public:
    HclDeviceControllerGaudi3(int fd, int numOfStreams);

private:
};