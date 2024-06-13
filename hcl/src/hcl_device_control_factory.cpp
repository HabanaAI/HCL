#include "hcl_device_control_factory.h"
#include "platform/gen2_arch_common/hcl_device_controller.h"
#include "platform/gaudi2/hcl_device_controller.h"
#include "platform/gaudi3/hcl_device_controller.h"
#include "platform/gen2_arch_common/hcl_device.h"
#include "platform/gaudi2/hcl_device.h"
#include "platform/gaudi3/hcl_device.h"
#include "platform/gaudi2/hal.h"
#include "platform/gaudi3/hal.h"

HclDeviceControllerGen2Arch* HclControlDeviceFactory::s_deviceController = nullptr;
IHclDevice*                  HclControlDeviceFactory::s_idevice          = nullptr;

IHclDevice* HclControlDeviceFactory::initFactory(synDeviceType deviceType, HclDeviceConfig* deviceConfig)
{
    int fd = deviceConfig ? deviceConfig->m_fd : -1;

    if (s_idevice == nullptr)
    {
        IHclDevice* idevice = nullptr;
        if (deviceType == synDeviceGaudi2)
        {
            hcl::Gaudi2Hal hal;
            s_deviceController = new HclDeviceControllerGaudi2(fd, hal.getMaxStreams());
            idevice            = deviceConfig ? new HclDeviceGaudi2(*s_deviceController, *deviceConfig) : nullptr;
        }
        else if (deviceType == synDeviceGaudi3)
        {
            hcl::Gaudi3Hal hal;
            s_deviceController = new HclDeviceControllerGaudi3(fd, hal.getMaxStreams());
            idevice            = deviceConfig ? new HclDeviceGaudi3(*s_deviceController, *deviceConfig) : nullptr;
        }
        else
        {
            VERIFY(false, "Invalid device type ({}) requested to generate controller.", deviceType);
        }
        s_idevice = idevice;
        s_deviceController->setDevice((HclDeviceGen2Arch*)s_idevice);
    }
    return s_idevice;
}

void HclControlDeviceFactory::destroyFactory(bool force)
{
    if (s_idevice != nullptr)
    {
        s_idevice->destroy(force);
    }
    delete s_idevice;
    s_idevice = nullptr;

    if (s_deviceController != nullptr)
    {
        delete s_deviceController;
    }
}

HclDeviceControllerGen2Arch& HclControlDeviceFactory::getDeviceControl()
{
    return *s_deviceController;
}
