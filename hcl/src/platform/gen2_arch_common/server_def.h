#pragma once

#include <set>      // for set
#include <cstdint>  // for uint*_t
#include <memory>   // for unique_ptr, shared_ptr

#include "platform/gen2_arch_common/server_connectivity_types.h"  // for
#include "platform/gen2_arch_common/server_connectivity.h"        // for Gen2ArchServerConnectivity
#include "hcl_types.h"                                            // for HCL_HwModuleId
#include "hcl_bits.h"                                             // for nics_mask_t
#include "interfaces/hcl_hal.h"                                   // for HalPtr
#include "platform/gen2_arch_common/hcl_device_controller.h"      // for HclDeviceControllerGen2Arch
#include "platform/gen2_arch_common/hcl_device.h"                 // for HclDeviceGen2Arch

// forward decl
class HclDeviceConfig;

namespace hcl
{
class Gen2ArchHal;
}

class Gen2ArchServerDef
{
public:
    Gen2ArchServerDef(const int        fd,
                      const int        moduleId,
                      const uint32_t   defaultBoxSize,
                      const uint32_t   defaultScaleupGroupSize,
                      HclDeviceConfig& deviceConfig,
                      const bool       isUnitTest = false);
    virtual ~Gen2ArchServerDef()                           = default;
    Gen2ArchServerDef(const Gen2ArchServerDef&)            = delete;
    Gen2ArchServerDef& operator=(const Gen2ArchServerDef&) = delete;

    virtual void      init() = 0;
    void              destroy();
    const DevicesSet& getHwModules() const { return m_hwModuleIds; }
    uint32_t          getDefaultBoxSize() const { return m_defaultBoxSize; }
    uint32_t          getDefaultScaleupGroupSize() const { return m_defaultScaleupGroupSize; }

    HclDeviceConfig&       getDeviceConfig() { return m_deviceConfig; }
    const HclDeviceConfig& getDeviceConfig() const { return m_deviceConfig; }

    const hcl::Hal& getHal() const { return *m_halShared; }
    hcl::HalPtr     getHalSharedPtr() { return m_halShared; }

    HclDeviceControllerGen2Arch&       getDeviceController() { return *m_deviceController; }
    const HclDeviceControllerGen2Arch& getDeviceController() const { return *m_deviceController; }

    HclDeviceGen2Arch&       getDevice() { return *m_device; }
    const HclDeviceGen2Arch& getDevice() const { return *m_device; }

    Gen2ArchServerConnectivity&       getServerConnectivity() { return *m_serverConnectivity; }
    const Gen2ArchServerConnectivity& getServerConnectivityConst() const { return *m_serverConnectivity; }

protected:
    const int        m_fd       = UNIT_TESTS_FD;        // this device FD, can stay -1 for unit tests
    const int        m_moduleId = UNDEFINED_MODULE_ID;  // This device module id, can stay -1 for unit tests
    const uint32_t   m_defaultBoxSize;
    const uint32_t   m_defaultScaleupGroupSize;  // Amount of Gaudis with any to any connectivity
    HclDeviceConfig& m_deviceConfig;
    const bool       m_isUnitTest;

    std::unique_ptr<Gen2ArchServerConnectivity> m_serverConnectivity = nullptr;
    DevicesSet                                  m_hwModuleIds;  // module ids inside the box with me

    hcl::HalPtr                                  m_halShared        = nullptr;
    std::unique_ptr<HclDeviceControllerGen2Arch> m_deviceController = nullptr;
    std::unique_ptr<HclDeviceGen2Arch>           m_device           = nullptr;

private:
    virtual void fillModuleIds();
};
