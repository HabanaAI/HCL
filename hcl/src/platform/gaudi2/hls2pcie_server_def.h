#pragma once

#include <cstdint>  // for uint8_t

#include "platform/gen2_arch_common/server_def.h"  // for Gen2ArchServerDef

class HclDeviceConfig;

class HLS2PCIEServerDef : public Gen2ArchServerDef
{
public:
    HLS2PCIEServerDef(const int fd, const int moduleId, HclDeviceConfig& deviceConfig, const bool isUnitTest = false);
    virtual ~HLS2PCIEServerDef()                           = default;
    HLS2PCIEServerDef(const HLS2PCIEServerDef&)            = delete;
    HLS2PCIEServerDef& operator=(const HLS2PCIEServerDef&) = delete;

    virtual void init() override;

protected:
private:
};
