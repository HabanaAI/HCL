#pragma once

#include <mutex>
#include <memory>  // for unique_ptr

#include "interfaces/hcl_hal.h"  // for HalPtr

class HclDeviceControllerGen2Arch;
class hccl_device_t;
class HclDeviceConfig;
class Gen2ArchServerDef;

class HclControlDeviceFactory
{
public:
    static hccl_device_t*               initDevice(HclDeviceConfig& deviceConfig);
    static HclDeviceControllerGen2Arch& getDeviceControl();
    static void                         destroyDevice(hccl_device_t* hcclDevice);

protected:
    static hcl::HalPtr                        s_halShared;
    static std::unique_ptr<Gen2ArchServerDef> s_serverDef;
};