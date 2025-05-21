#pragma once
#include "platform/gaudi_common/hcl_device_controller.h"  // for HclDeviceControllerGaudiCommon

class HclDeviceControllerGaudi3 : public HclDeviceControllerGaudiCommon
{
public:
    HclDeviceControllerGaudi3(const int fd, const unsigned numOfStreams);
    virtual ~HclDeviceControllerGaudi3()                                   = default;
    HclDeviceControllerGaudi3(const HclDeviceControllerGaudi3&)            = delete;
    HclDeviceControllerGaudi3& operator=(const HclDeviceControllerGaudi3&) = delete;

    virtual void clearSimb(HclDeviceGen2Arch* device, uint8_t apiID);
};