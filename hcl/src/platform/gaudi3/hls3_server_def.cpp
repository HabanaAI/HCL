#include "platform/gaudi3/hls3_server_def.h"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint*_t
#include <memory>   // for unique_ptr, shared_ptr

#include "platform/gaudi3/hls3_server_connectivity.h"  // for HLS3ServerConnectivity
#include "platform/gaudi3/server_autogen_HLS3.h"
#include "platform/gen2_arch_common/hal.h"                    // for Gen2ArchHal
#include "platform/gen2_arch_common/hcl_device_controller.h"  // for HclDeviceControllerGen2Arch
#include "platform/gen2_arch_common/hcl_device.h"             // for HclDeviceGen2Arch
#include "platform/gaudi3/hcl_device.h"                       // for HclDeviceGaudi3
#include "platform/gaudi3/hal.h"                              // for Gaudi3Hal
#include "platform/gaudi3/hcl_device_controller.h"            // for HclDeviceControllerGaudi3
#include "platform/gen2_arch_common/hcl_device_config.h"      // for HclDeviceConfig
#include "interfaces/hcl_hal.h"                               // for HalPtr
#include "platform/gen2_arch_common/server_def.h"             // for Gen2ArchServerDef

#include "hcl_utils.h"        // for LOG_HCL_*
#include "hcl_log_manager.h"  // for LOG_*

HLS3ServerDef::HLS3ServerDef(const int fd, const int moduleId, HclDeviceConfig& deviceConfig, const bool isUnitTest)
: Gen2ArchServerDef(fd, moduleId, HLS3_NUM_DEVICES, HLS3_SCALEUP_GROUP_SIZE, deviceConfig, isUnitTest)
{
    LOG_HCL_DEBUG(HCL, "ctor, fd={}, moduleId={}, isUnitTest={}", fd, moduleId, isUnitTest);
}

void HLS3ServerDef::init()
{
    LOG_HCL_DEBUG(HCL, "Started");
    m_serverConnectivity =
        std::make_unique<HLS3ServerConnectivity>(m_fd, m_moduleId, false /*useDummyConnectivity*/, m_deviceConfig);
    m_serverConnectivity->init(!m_isUnitTest);

    m_halShared        = std::make_shared<hcl::Gaudi3Hal>();
    m_deviceController = std::make_unique<HclDeviceControllerGaudi3>(m_fd, m_halShared->getMaxArchStreams());
    m_device = m_fd >= 0 ? std::make_unique<HclDeviceGaudi3>(*m_deviceController, m_deviceConfig, m_halShared, *this)
                         : nullptr;
}
