#pragma once

#include <memory>  // for unique_ptr

class HclDeviceConfig;

class HclDeviceConfigFactory
{
public:
    static std::unique_ptr<HclDeviceConfig> createDeviceConfig();
};