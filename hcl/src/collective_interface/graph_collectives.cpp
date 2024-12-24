#include "hcl_collective_params.h"
#include "platform/gen2_arch_common/collective_states.h"
#include "collective_interface/hccl_graph.h"
#include "collective_interface/graph_collectives.h"
#include "collective_interface/collectives/all_gather.h"

static const std::map<HCL_CollectiveOp,
                      std::function<hcclResult_t(IHcclGraphEngine* engine, HclCollectiveParams& params)>>
    methodsMap = {{eHCLAllGather, agRunRing}};

hcclResult_t run(IHcclGraphEngine* engine, HclCollectiveParams& params)
{
    VERIFY(methodsMap.count(params.m_collectiveOp) > 0, "Collective is not defined in primitive pattern");
    return methodsMap.at(params.m_collectiveOp)(engine, params);
}