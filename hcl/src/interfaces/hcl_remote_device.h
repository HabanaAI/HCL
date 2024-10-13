#pragma once

#include <cstdint>  // for uint32_t, uint16_t, uint64_t
#include <array>    // for array
#include <list>     // for list
#include <map>      // for map
#include <memory>   // for allocator, unique_ptr

#include "hcl_api_types.h"  // for HCL_Rank
#include "hcl_types.h"      // for RankInfo

struct HclRemoteDevice : public RemoteDeviceConnectionInfo
{
    bool             m_initialized = false;
    HclRemoteDevice& operator=(const RemoteDeviceConnectionInfo& other)
    {
        *((RemoteDeviceConnectionInfo*)this) = other;
        return *this;
    }
};

using HclRemoteDeviceArray = std::vector<std::unique_ptr<HclRemoteDevice>>;
