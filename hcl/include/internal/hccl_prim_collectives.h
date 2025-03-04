#pragma once
#include <map>
#include <functional>
#include "hccl_types.h"
#include "hcl_api_types.h"

#define CHECK_PRIM_IMPL(op) (unlikely((GCFG_HCCL_PRIM_COLLECTIVE_MASK.value() & (1 << op)) > 0))

class IHcclGraphEngine;
struct HclCollectiveParams;

using primCollectiveImpl_t =
    std::map<HCL_CollectiveOp, std::function<hcclResult_t(IHcclGraphEngine* engine, HclCollectiveParams& params)>>;

namespace HcclPrimitives
{
extern primCollectiveImpl_t*                        extendedMethods;
__attribute__((visibility("default"))) bool         checkPrimitiveImpl(HCL_CollectiveOp op);
__attribute__((visibility("default"))) hcclResult_t initPrimitiveImpl();
hcclResult_t                                        run(IHcclGraphEngine* engine, HclCollectiveParams& params);
}  // namespace HcclPrimitives
