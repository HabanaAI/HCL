#pragma once

#include <cstdint>  // for uint8_t

#include "platform/gen2_arch_common/server_def.h"  // for Gen2ArchServerDef

class HclDeviceConfig;

class HLS3PCIEServerDef : public Gen2ArchServerDef
{
public:
    HLS3PCIEServerDef(const int fd, const int moduleId, HclDeviceConfig& deviceConfig, const bool isUnitTest = false);
    virtual ~HLS3PCIEServerDef()                           = default;
    HLS3PCIEServerDef(const HLS3PCIEServerDef&)            = delete;
    HLS3PCIEServerDef& operator=(const HLS3PCIEServerDef&) = delete;

    virtual void init() override;

protected:
private:
    virtual void fillModuleIds() override;
};
