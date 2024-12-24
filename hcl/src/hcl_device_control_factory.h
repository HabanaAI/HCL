#pragma once

#include <memory>  // for unique_ptr

class HclDeviceControllerGen2Arch;
class hccl_device_t;
class HclDeviceConfig;
class Gen2ArchServerDef;
class FaultInjectionDevice;

class HclControlDeviceFactory
{
public:
    static hccl_device_t*               initDevice(HclDeviceConfig& deviceConfig);
    static HclDeviceControllerGen2Arch& getDeviceControl();
    static void                         destroyDevice(hccl_device_t* hcclDevice);

protected:
    static std::unique_ptr<Gen2ArchServerDef>    s_serverDef;
    static std::unique_ptr<FaultInjectionDevice> s_faultsInjectionServer;
};