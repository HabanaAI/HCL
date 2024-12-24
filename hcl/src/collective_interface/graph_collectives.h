#pragma once
#include "hccl_types.h"

#define CHECK_PRIM_IMPL(op) (unlikely((GCFG_HCCL_PRIM_COLLECTIVE_MASK.value() & (1 << op)) > 0))

class IHcclGraphEngine;
struct HclCollectiveParams;

hcclResult_t run(IHcclGraphEngine* engine, HclCollectiveParams& params);
