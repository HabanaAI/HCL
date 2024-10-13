#include "hcl_device_control_factory.h"

#include <memory>  // for unique_ptr

#include "synapse_common_types.h"                             // for synDeviceType
#include "platform/gaudi_common/hcl_device_config.h"          // for HclDeviceConfigGaudiCommon
#include "platform/gen2_arch_common/hcl_device_controller.h"  // for HclDeviceControllerGen2Arch
#include "platform/gaudi2/hcl_device_controller.h"            // for HclDeviceControllerGaudi2
#include "platform/gaudi3/hcl_device_controller.h"            // for HclDeviceControllerGaudi3
#include "interfaces/hcl_idevice.h"                           // for IHclDevice
#include "platform/gen2_arch_common/hcl_device.h"             // for HclDeviceGen2Arch
#include "platform/gaudi2/hccl_device.h"                      // for hccl_gaudi2_t
#include "platform/gaudi3/hccl_device.h"                      // for hccl_gaudi3_t
#include "platform/gaudi2/hcl_device.h"                       // for HclDeviceGaudi2
#include "platform/gaudi3/hcl_device.h"                       // for HclDeviceGaudi3
#include "platform/gaudi2/hal.h"                              // for Gaudi2Hal
#include "platform/gaudi3/hal.h"                              // for Gaudi3Hal
#include "platform/gaudi3/hal_hls3pcie.h"                     // for Gaudi3Hls3PCieHal
#include "hcl_global_conf.h"                                  // for GCFG_BOX_TYPE_ID
#include "hcl_types.h"                                        // for HclConfigType
#include "interfaces/hcl_hal.h"                               // for HalPtr
#include "platform/gen2_arch_common/server_def.h"             // for Gen2ArchServerDef
#include "platform/gen2_arch_common/server_connectivity.h"    // for Gen2ArchServerConnectivity
#include "platform/gen2_arch_common/runtime_connectivity.h"   // for Gen2ArchRuntimeConnectivity
#include "platform/gaudi3/hls3_server_def.h"                  // for HLS3ServerDef
#include "platform/gaudi3/hls3pcie_server_def.h"              // for HLS3PCIEServerDef
#include "platform/gaudi2/hls2_server_def.h"                  // for HLS2ServerDef

hcl::HalPtr                        HclControlDeviceFactory::s_halShared = nullptr;
std::unique_ptr<Gen2ArchServerDef> HclControlDeviceFactory::s_serverDef = nullptr;

hccl_device_t* HclControlDeviceFactory::initDevice(HclDeviceConfig& deviceConf)
{
    HclDeviceConfigGaudiCommon& deviceConfig = dynamic_cast<HclDeviceConfigGaudiCommon&>(deviceConf);

    VERIFY(s_serverDef == nullptr, "Expected s_serverDef to be uninitialized");

    const synDeviceType deviceType = deviceConfig.getDeviceType();
    const int           fd         = deviceConfig.getFd();

    const HclConfigType configType = (HclConfigType)GCFG_BOX_TYPE_ID.value();
    LOG_DEBUG(HCL, "{}: Creating server based on configType={}, deviceType={}", __FUNCTION__, configType, deviceType);

    switch (configType)
    {
        case HLS2:
            s_halShared = std::make_shared<hcl::Gaudi2Hal>();
            s_serverDef = std::make_unique<HLS2ServerDef>(fd, deviceConfig.getHwModuleId(), deviceConfig, false);
            break;
        case HLS3:
            s_halShared = std::make_shared<hcl::Gaudi3Hal>();
            s_serverDef = std::make_unique<HLS3ServerDef>(fd, deviceConfig.getHwModuleId(), deviceConfig, false);
            break;
        case HL338:
            s_halShared = std::make_shared<hcl::Gaudi3Hls3PCieHal>(deviceConfig.getHwModuleId());
            s_serverDef = std::make_unique<HLS3PCIEServerDef>(fd, deviceConfig.getHwModuleId(), deviceConfig, false);
            break;
        // support special modes and unit tests
        case LOOPBACK:
        case BACK_2_BACK:
        case RING:
        case UNKNOWN:
            if (deviceType == synDeviceGaudi2)
            {
                s_halShared = std::make_shared<hcl::Gaudi2Hal>();
                s_serverDef = std::make_unique<HLS2ServerDef>(fd, deviceConfig.getHwModuleId(), deviceConfig, false);
            }
            else if (deviceType == synDeviceGaudi3)
            {
                s_halShared = std::make_shared<hcl::Gaudi3Hal>();
                s_serverDef = std::make_unique<HLS3ServerDef>(fd, deviceConfig.getHwModuleId(), deviceConfig, false);
            }
            else
            {
                VERIFY(false, "Unsupported device type {} for configType={}", deviceType, configType);
            }
            break;
        default:
            VERIFY(false, "Invalid server type ({}) requested to generate controller.", configType);
    }

    if (deviceConfig.getFd() >= 0)
    {
        VERIFY(g_ibv.init(deviceConfig) == hcclSuccess, "ibv initialization failed");
    }

    hccl_device_t* hcclDevice = nullptr;
    s_serverDef->init();
    if (deviceType == synDeviceGaudi2)
    {
        hcclDevice = new hccl_gaudi2_t((HclDeviceGaudi2*)&(s_serverDef->getDevice()));
    }
    else if (deviceType == synDeviceGaudi3)
    {
        hcclDevice = new hccl_gaudi3_t((HclDeviceGaudi3*)&(s_serverDef->getDevice()));
    }
    else
    {
        VERIFY(false, "Invalid device type ({}) requested to generate controller.", deviceType);
    }

    s_serverDef->getDeviceController().setDevice((HclDeviceGen2Arch*)(&(s_serverDef->getDevice())));
    return hcclDevice;
}

void HclControlDeviceFactory::destroyDevice(hccl_device_t* hcclDevice)
{
    LOG_DEBUG(HCL, "{}: Called", __FUNCTION__);
    if (hcclDevice && hcclDevice->initialized)
    {
        delete hcclDevice;
    }

    s_serverDef->destroy();
    s_serverDef.reset(nullptr);

    g_ibv.close();
}

HclDeviceControllerGen2Arch& HclControlDeviceFactory::getDeviceControl()
{
    VERIFY(s_serverDef != nullptr);
    return s_serverDef->getDeviceController();
}
