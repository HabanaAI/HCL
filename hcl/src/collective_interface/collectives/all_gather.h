#pragma once
#include "hccl_types.h"
#include "hcl_collective_params.h"
#include "platform/gen2_arch_common/collective_states.h"

class IHcclGraphEngine;
class HcclGraph;

hcclResult_t agRunPairwise(IHcclGraphEngine* engine, HclCollectiveParams& params);
hcclResult_t agRunRing(IHcclGraphEngine* engine, HclCollectiveParams& params);