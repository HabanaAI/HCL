#include "platform/gaudi3/hls3rack_server_def.h"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint*_t
#include <memory>   // for unique_ptr, shared_ptr

#include "platform/gen2_arch_common/server_connectivity_types.h"  // for
#include "platform/gaudi3/hls3rack_server_connectivity.h"         // for HLS3RackServerConnectivity
#include "platform/gaudi3/server_autogen_HLS3Rack.h"
#include "platform/gen2_arch_common/hal.h"                    // for Gen2ArchHal
#include "platform/gen2_arch_common/hcl_device_controller.h"  // for HclDeviceControllerGen2Arch
#include "platform/gen2_arch_common/hcl_device.h"             // for HclDeviceGen2Arch
#include "platform/gen2_arch_common/hcl_device_config.h"      // for HclDeviceConfig
#include "platform/gaudi3/hcl_device.h"                       // for HclDeviceGaudi3
#include "platform/gaudi3/hal.h"                              // for Gaudi3Hal
#include "platform/gaudi3/hcl_device_controller.h"            // for HclDeviceControllerGaudi3
#include "interfaces/hcl_hal.h"                               // for HalPtr

#include "hcl_utils.h"        // for LOG_HCL_*
#include "hcl_log_manager.h"  // for LOG_*

HLS3RackServerDef::HLS3RackServerDef(const int        fd,
                                     const int        moduleId,
                                     HclDeviceConfig& deviceConfig,
                                     const bool       isUnitTest)
: Gen2ArchServerDef(fd, moduleId, HLS3RACK_NUM_DEVICES, HLS3RACK_SCALEUP_GROUP_SIZE, deviceConfig, isUnitTest)
{
    fillModuleIds();  // Overwrite parent class defaults
    LOG_HCL_DEBUG(HCL,
                  "ctor, fd={}, moduleId={}, isUnitTest={}, m_hwModuleIds={}",
                  fd,
                  moduleId,
                  isUnitTest,
                  m_hwModuleIds);
}

void HLS3RackServerDef::init()
{
    LOG_HCL_DEBUG(HCL, "Started");
    m_serverConnectivity =
        std::make_unique<HLS3RackServerConnectivity>(m_fd, m_moduleId, false /*useDummyConnectivity*/, m_deviceConfig);
    m_serverConnectivity->init(true);

    m_halShared        = std::make_shared<hcl::Gaudi3Hal>();
    m_deviceController = std::make_unique<HclDeviceControllerGaudi3>(m_fd, m_halShared->getMaxArchStreams());
    m_device = m_fd >= 0 ? std::make_unique<HclDeviceGaudi3>(*m_deviceController, m_deviceConfig, m_halShared, *this)
                         : nullptr;
}

void HLS3RackServerDef::fillModuleIds()
{
    m_hwModuleIds.clear();
    const HCL_HwModuleId moduleIdForFill = m_moduleId >= 0 ? (HCL_HwModuleId)m_moduleId : 0;
    HCL_HwModuleId       n((moduleIdForFill >= HLS3RACK_SCALEUP_GROUP_SIZE) ? HLS3RACK_SCALEUP_GROUP_SIZE : 0);
    std::generate_n(std::inserter(m_hwModuleIds, m_hwModuleIds.begin()), HLS3RACK_SCALEUP_GROUP_SIZE, [n]() mutable {
        return n++;
    });
}
