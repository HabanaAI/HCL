#pragma once

#include "hcl_utils.h"
#include "hcl_api_types.h"
#include "hcl_types.h"
#include "hcl_collective_params.h"
#include "hcl_api_entry.h"
#include "hccl_types.h"

class IHclCollectiveRoutines
{
public:
    virtual ~IHclCollectiveRoutines() noexcept(false) {};

    virtual hcclResult_t hclCollectiveCall(HclCollectiveParams& params) = 0;
};
