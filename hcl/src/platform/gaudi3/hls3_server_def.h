#pragma once

#include <cstdint>  // for uint8_t

#include "platform/gen2_arch_common/server_def.h"  // for Gen2ArchServerDef

class HclDeviceConfig;

class HLS3ServerDef : public Gen2ArchServerDef
{
public:
    HLS3ServerDef(const int fd, const int moduleId, HclDeviceConfig& deviceConfig, const bool isUnitTest = false);
    virtual ~HLS3ServerDef()                       = default;
    HLS3ServerDef(const HLS3ServerDef&)            = delete;
    HLS3ServerDef& operator=(const HLS3ServerDef&) = delete;

    virtual void init() override;

protected:
private:
};
