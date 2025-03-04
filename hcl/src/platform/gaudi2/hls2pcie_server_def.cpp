#include "platform/gaudi2/hls2pcie_server_def.h"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint*_t
#include <memory>   // for unique_ptr

#include "platform/gaudi2/hls2pcie_server_connectivity.h"  // for HLS2PCIEServerConnectivity
#include "platform/gaudi2/server_autogen_HLS2PCIE.h"
#include "platform/gen2_arch_common/hal.h"                    // for Gen2ArchHal
#include "platform/gen2_arch_common/hcl_device_controller.h"  // for HclDeviceControllerGen2Arch
#include "platform/gen2_arch_common/hcl_device.h"             // for HclDeviceGen2Arch
#include "platform/gen2_arch_common/hcl_device_config.h"      // for HclDeviceConfig
#include "platform/gaudi2/hcl_device_controller.h"            // for HclDeviceControllerGaudi2
#include "platform/gaudi2/hcl_device.h"                       // for HclDeviceGaudi2
#include "platform/gaudi2/hal.h"                              // for Gaudi2Hal
#include "hcl_utils.h"                                        // for LOG_HCL_*
#include "hcl_log_manager.h"                                  // for LOG_*

HLS2PCIEServerDef::HLS2PCIEServerDef(const int        fd,
                                     const int        moduleId,
                                     HclDeviceConfig& deviceConfig,
                                     const bool       isUnitTest)
: Gen2ArchServerDef(fd, moduleId, HLS2PCIE_NUM_DEVICES, HLS2PCIE_SCALEUP_GROUP_SIZE, deviceConfig, isUnitTest)
{
    LOG_HCL_DEBUG(HCL, "ctor, fd={}, moduleId={}, isUnitTest={}", fd, moduleId, isUnitTest);
}

void HLS2PCIEServerDef::init()
{
    LOG_HCL_DEBUG(HCL, "Started");
    m_serverConnectivity =
        std::make_unique<HLS2PCIEServerConnectivity>(m_fd, m_moduleId, false /*useDummyConnectivity*/, m_deviceConfig);
    m_serverConnectivity->init(true);

    m_halShared        = std::make_shared<hcl::Gaudi2Hal>();
    m_deviceController = std::make_unique<HclDeviceControllerGaudi2>(m_fd, m_halShared->getMaxStreams());
    m_device = m_fd >= 0 ? std::make_unique<HclDeviceGaudi2>(*m_deviceController, m_deviceConfig, m_halShared, *this)
                         : nullptr;
}
