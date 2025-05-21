#pragma once

#include <cstdint>        // for uint*
#include <array>          // for array
#include <vector>         // for vector
#include <unordered_set>  // for unordered_set

#include "platform/gen2_arch_common/server_connectivity_types.h"  //
#include "gaudi3/gaudi3.h"                                        // for NIC_MAX_NUM_OF_MACROS
#include "hcl_types.h"                                            // for HCL_HwModuleId
#include "platform/gaudi3/server_autogen_HLS3.h"                  // for HLS3_* consts
#include "platform/gaudi3/server_autogen_HLS3PCIE.h"              // for HLS3PCIE_* consts
#include "platform/gaudi3/server_autogen_HLS3Rack.h"              // for HLS3RACK_* consts

static_assert(HLS3_NUM_NICS == NIC_MAX_NUM_OF_MACROS * 2, "HLS3 nics count must match G3 NIC_MAX_NUM_OF_MACROS*2");
static_assert(HLS3PCIE_NUM_NICS == NIC_MAX_NUM_OF_MACROS * 2,
              "HLS3PCIE nics count must match G3 NIC_MAX_NUM_OF_MACROS*2");
static_assert(HLS3RACK_NUM_NICS == NIC_MAX_NUM_OF_MACROS * 2,
              "HLS3RACK nics count must match G3 NIC_MAX_NUM_OF_MACROS*2");

typedef std::vector<uint32_t> RemoteDevicePortMasksVector;  // 24 bits per device

typedef std::vector<uint16_t> DeviceNicsMacrosMask;  // per device module id, a dup mask with bit set for nic macro it
                                                     // belongs to. (Only scaleup nic macros appear here)

typedef uint16_t                        NicMacroIndexType;
typedef std::vector<NicMacroIndexType>  NicMacrosPerDevice;      // vector of nic macro indexes
typedef std::vector<NicMacrosPerDevice> NicMacrosDevicesVector;  // an vector of vectors of macros indexes for all
                                                                 // devices. Only scaleup related nic macros appear here

typedef enum
{
    NIC_MACRO_NO_SCALEUP_NICS = 0,
    NIC_MACRO_NOT_CONNECTED_NICS,
    NIC_MACRO_SINGLE_SCALEUP_NIC,
    NIC_MACRO_TWO_SCALEUP_NICS
} NicMacroPairNicsConfig;

struct NicMacroPair
{
    uint32_t               m_device0    = 0;  // always have value
    uint32_t               m_device1    = 0;  // may have value if shared
    NicMacroPairNicsConfig m_nicsConfig = NIC_MACRO_NO_SCALEUP_NICS;
};

typedef std::array<struct NicMacroPair, NIC_MAX_NUM_OF_MACROS>
    NicMacroPairs;  // All the nic macros pairs of specific device
