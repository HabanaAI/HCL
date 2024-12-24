#include "collective_interface/prims/hccl_prim.h"
#include "collective_interface/hccl_graph.h"

bool hcclSyncInfo::isCrossExec()
{
    return m_signaler->execSet() != m_waiter->execSet();
}

void HcclPrim::init(HcclGraph* graph, int idx)
{
    m_graph   = graph;
    m_primIdx = idx;
}

void HcclPrim::setExecSet(int execSet)
{
    m_execSet = execSet;
}

WaitMethod HcclPrim::getWaitResource(bool incRequest)
{
    if (hasWait() && signalers()[0]->syncMethod != WaitMethod::WAIT_METHOD_MAX)
    {
        return signalers()[0]->syncMethod;
    }
    else
    {
        return (WaitMethod)((int)WaitMethod::GPSO_0 + m_graph->getWaits(incRequest));
    }
}

bool HcclPrim::isStrongOrderRequired()
{
    return std::any_of(signalers().cbegin(), signalers().cend(), [](hcclSyncInfo* sync) {
        return sync->isCrossExec();
    });
}