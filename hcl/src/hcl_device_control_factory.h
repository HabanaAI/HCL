#pragma once
#include "interfaces/hcl_idevice.h"
#include <mutex>

class HclDeviceControllerGen2Arch;
class IHclDevice;
class HclDeviceConfig;

class HclControlDeviceFactory
{
public:
    static IHclDevice*                  initFactory(synDeviceType deviceType, HclDeviceConfig* deviceConfig = nullptr);
    static HclDeviceControllerGen2Arch& getDeviceControl();
    static void                         destroyFactory(bool force = false);

private:
    static IHclDevice*                  s_idevice;
    static HclDeviceControllerGen2Arch* s_deviceController;
};