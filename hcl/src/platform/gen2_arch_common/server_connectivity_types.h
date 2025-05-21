#pragma once

#include <array>    // for array
#include <cstdint>  // for uint*_t
#include <tuple>    // for tuple

#include "hcl_api_types.h"                    // for HCL_Comm
#include "platform/gen2_arch_common/types.h"  // for MAX_NICS_GEN2ARCH

// <remote card location, remote nic, sub nic index>
using Gen2ArchNicDescriptor =
    std::tuple<unsigned, uint8_t, uint8_t>;  // remote device id (-1 for SO, -2 not connected), remote nic in
                                             // device, remote sub-nic index (0-2)

typedef std::array<Gen2ArchNicDescriptor, MAX_NICS_GEN2ARCH>
    Gen2ArchNicsDeviceSingleConfig;  // array of remote nics per current device nics
typedef std::vector<Gen2ArchNicsDeviceSingleConfig> ServerNicsConnectivityVector;

constexpr unsigned SCALEOUT_DEVICE_ID      = -1;
constexpr unsigned NOT_CONNECTED_DEVICE_ID = -2;
constexpr unsigned MAX_SUB_NICS            = 6;

constexpr HCL_Comm DEFAULT_COMM_ID = 0;

constexpr int UNDEFINED_MODULE_ID = -1;
constexpr int UNIT_TESTS_FD       = -1;
