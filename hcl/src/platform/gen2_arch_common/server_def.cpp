#include "platform/gen2_arch_common/server_def.h"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint*_t
#include <memory>   // for allocator_traits<>::value_type

#include "hcl_utils.h"                                        // for VERIFY
#include "platform/gen2_arch_common/server_connectivity.h"    // for Gen2ArchServerConnectivity
#include "platform/gen2_arch_common/runtime_connectivity.h"   // for Gen2ArchRuntimeConnectivity
#include "interfaces/hcl_hal.h"                               // for HalPtr
#include "platform/gen2_arch_common/hal.h"                    // for Gen2ArchHal
#include "platform/gen2_arch_common/hcl_device_controller.h"  // for HclDeviceControllerGen2Arch
#include "platform/gen2_arch_common/hcl_device.h"             // for HclDeviceGen2Arch
#include "platform/gen2_arch_common/hcl_device_config.h"      // for HclDeviceConfig

#include "hcl_log_manager.h"  // for LOG_*

Gen2ArchServerDef::Gen2ArchServerDef(const int        fd,
                                     const int        moduleId,
                                     const uint32_t   defaultBoxSize,
                                     const uint32_t   defaultScaleupGroupSize,
                                     HclDeviceConfig& deviceConfig,
                                     const bool       isUnitTest)
: m_fd(fd),
  m_moduleId(moduleId),
  m_defaultBoxSize(defaultBoxSize),
  m_defaultScaleupGroupSize(defaultScaleupGroupSize),
  m_deviceConfig(deviceConfig),
  m_isUnitTest(isUnitTest)
{
    LOG_HCL_DEBUG(HCL,
                  "ctor, fd={}, moduleId={}, defaultBoxSize={}, defaultScaleupGroupSize={}, isUnitTest={}",
                  fd,
                  moduleId,
                  defaultBoxSize,
                  defaultScaleupGroupSize,
                  isUnitTest);
    fillModuleIds();
}

void Gen2ArchServerDef::fillModuleIds()
{
    m_hwModuleIds.clear();
    HCL_HwModuleId n(0);
    std::generate_n(std::inserter(m_hwModuleIds, m_hwModuleIds.begin()), m_defaultBoxSize, [n]() mutable {
        return n++;
    });
}

void Gen2ArchServerDef::destroy()
{
    LOG_HCL_DEBUG(HCL, "dtor, m_fd={}, m_moduleId={}, m_isUnitTest={}", m_fd, m_moduleId, m_isUnitTest);
    if (!m_isUnitTest && m_device != nullptr)
    {
        m_device->destroy();
    }
    m_device.reset(nullptr);
    m_deviceController.reset(nullptr);
    m_serverConnectivity.reset(nullptr);
}
