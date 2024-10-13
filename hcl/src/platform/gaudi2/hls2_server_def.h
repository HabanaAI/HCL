#pragma once

#include <cstdint>  // for uint8_t

#include "platform/gen2_arch_common/server_def.h"  // for Gen2ArchServerDef

class HclDeviceConfig;

class HLS2ServerDef : public Gen2ArchServerDef
{
public:
    HLS2ServerDef(const int fd, const int moduleId, HclDeviceConfig& deviceConfig, const bool isUnitTest = false);
    virtual ~HLS2ServerDef()                       = default;
    HLS2ServerDef(const HLS2ServerDef&)            = delete;
    HLS2ServerDef& operator=(const HLS2ServerDef&) = delete;

    virtual void init() override;

protected:
private:
};
