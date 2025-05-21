#pragma once

#include <cstdint>  // for uint8_t

#include "platform/gen2_arch_common/server_def.h"  // for Gen2ArchServerDef

class HclDeviceConfig;

class HLS3RackServerDef : public Gen2ArchServerDef
{
public:
    HLS3RackServerDef(const int fd, const int moduleId, HclDeviceConfig& deviceConfig, const bool isUnitTest = false);
    virtual ~HLS3RackServerDef()                           = default;
    HLS3RackServerDef(const HLS3RackServerDef&)            = delete;
    HLS3RackServerDef& operator=(const HLS3RackServerDef&) = delete;

    virtual void init() override;

protected:
private:
    virtual void fillModuleIds() override;
};
