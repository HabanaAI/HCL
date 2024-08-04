#pragma once
#include <cstdint>
#include <array>
#include "synapse_api_types.h"
#include "hcl_api_types.h"
#include "hccl_types.h"

enum ApiType
{
    Send         = 0,
    Recv         = 1,
    CollectiveOp = 2
};

struct SendRecvApiEntry
{
    ApiType         apiType;
    uint8_t         apiId;
    synStreamHandle streamHandle;
    uint64_t        address;
    uint64_t        count;
    hcclDataType_t  dataType;
    HCL_Rank        remoteRank;
    HCL_Comm        comm;
    uint32_t        hwModuleID;
    bool            isRankInsideScaleupGroup;
    bool            isValid = false;
    bool            isLast  = false;
};

struct SendRecvMemCopyEntry
{
    uint64_t       chunkCount      = 0;
    hcclDataType_t dataType        = hcclNumTypes;
    uint64_t       recvBaseAddress = 0;
    uint64_t       sendBaseAddress = 0;
};