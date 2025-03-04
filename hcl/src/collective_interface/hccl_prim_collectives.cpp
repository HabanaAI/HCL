#include "hcl_collective_params.h"
#include "collective_interface/hccl_graph.h"
#include "hccl_prim_collectives.h"
#include "collective_interface/collectives/all_reduce.h"

static primCollectiveImpl_t methodsMap = {{eHCLAllReduce, ar_runPairwise}};

namespace HcclPrimitives
{
primCollectiveImpl_t* extendedMethods = nullptr;

hcclResult_t run(IHcclGraphEngine* engine, HclCollectiveParams& params)
{
    VERIFY(HcclPrimitives::checkPrimitiveImpl(params.m_collectiveOp),
           "Collective {} is not implemented with primitives",
           params.m_collectiveOp);
    return HcclPrimitives::extendedMethods->at(params.m_collectiveOp)(engine, params);
}

hcclResult_t initPrimitiveImpl()
{
    if (HcclPrimitives::extendedMethods == nullptr)
    {
        HcclPrimitives::extendedMethods = &methodsMap;
    }
    else
    {
        HcclPrimitives::extendedMethods->insert(methodsMap.begin(), methodsMap.end());
    }
    return hcclSuccess;
}

bool checkPrimitiveImpl(HCL_CollectiveOp op)
{
    VERIFY(HcclPrimitives::extendedMethods != nullptr, "extendedMethods is null! must initPrimitiveImpl first.");
    return HcclPrimitives::extendedMethods->count(op) > 0;
}
}  // namespace HcclPrimitives
