#pragma once
#include "hccl_types.h"
#include "hcl_collective_params.h"

class IHcclGraphEngine;

hcclResult_t ar_runPairwise(IHcclGraphEngine* engine, HclCollectiveParams& params);