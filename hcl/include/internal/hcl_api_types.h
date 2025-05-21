/***************************************************************************
 * Copyright (C) 2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 * Unauthorized copying of this file, via any medium is strictly prohibited.
 * Proprietary and confidential.
 ****************************************************************************
 */

#pragma once

#include <cstdint>
#include <string_view>

// Allows for creating different communicators. Initially, use HCL_COMM_WORLD reserved name only.

typedef uint32_t HCL_Comm;
#define HCL_COMM_WORLD 0

typedef enum HCL_CollectiveOp
{
    eHCLReduce              = 0,
    eHCLAllReduce           = 1,
    eHCLReduceScatter       = 2,
    eHCLAll2All             = 3,
    eHCLBroadcast           = 4,
    eHCLAllGather           = 5,
    eHCLGather              = 6,
    eHCLScatter             = 7,
    eHCLSimpleBroadcast     = 8,
    eHCLNoCollective        = 9,  // used in Gaudi2 for Send/Recv operations
    eHCLSinglePeerBroadcast = 10,
    eHCLCollectiveLastValue  // Used to mark end of enum, do not change enum position or use as value
} HCL_CollectiveOp;

typedef enum HCL_Flags
{
    eHCLNoFlag    = 0x00,
    eHCLWeakOrder = 1 << 0,
    eHCCLAPICall  = 1 << 1
} HCL_Flags;

// clang-format off
union hcl_flags_t
{
    //     eHCLWeakOrder       = 1 << 0
    struct
    {
        uint32_t weak_order          : 1;  // 0 - 0
        uint32_t hccl_api_call       : 1;  // 1 - 1
        uint32_t reserved            : 30; // 2 - 31;
    };
    uint32_t flags = 0;
    hcl_flags_t(const uint32_t _flags) : flags(_flags) {}
    operator uint32_t&() { return flags; }
};
// clang-format on

struct HCL_Request
{
    uint64_t event  = 0;
    uint64_t index  = 0;
    uint64_t pIndex = 0;
};

#define HCL_UNIQUE_ID_MAX_SIZE 1024
struct HCL_UniqueId
{
    char     internal[HCL_UNIQUE_ID_MAX_SIZE];
    uint32_t length = 0;
};

const uint64_t HCL_InfinityWait   = 0xFFFFFFFFFFFFFFFF;
const uint8_t  HCL_DEFAULT_API_ID = 0;

struct CommIds
{
    HCL_Comm               commId;
    const std::string_view commIdPort;
};
