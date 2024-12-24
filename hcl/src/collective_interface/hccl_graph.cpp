
#include "collective_interface/prims/hccl_prim.h"
#include "collective_interface/hccl_graph.h"

HcclGraph::HcclGraph(IHcclGraphEngine* engine, HclCollectiveParams* params) : m_graphEngine(engine), m_params(params) {}

void HcclGraph::addWait(hcclPrim_t signaler, hcclPrim_t waiter)
{
    VERIFY(signaler->primIdx() < waiter->primIdx(),
           "Illegal usage of addWait, signaler prim must be created before waiter");
    syncs().emplace_back(/*signaler=*/signaler, /*waiter=*/waiter);
    hcclSyncInfo* sync = &(syncs().back());
    waiter->signalers().push_back(sync);
    signaler->waiters().push_back(sync);
}

void HcclGraph::setupExecSets()
{
    m_executionSets.emplace_back();
    uint8_t prevTypeMask = 0, typeMask = 0;
    for (hcclPrim_t prim : m_prims)
    {
        if (prim->execSet() >= 0) continue;  // already set
        else
        {
            std::queue<hcclPrim_t>  prims;
            std::vector<hcclPrim_t> subGraph;
            prims.push(prim);

            while (!prims.empty())
            {
                auto temp = prims.front();
                typeMask |= (1 << temp->type());
                for (hcclSyncInfo* sync : temp->waiters())
                {
                    prims.push(sync->m_waiter);
                }
                prims.pop();

                if ((prevTypeMask & (1 << temp->type())) == 0)
                {
                    subGraph.push_back(temp);
                }
                else
                {
                    typeMask &= (~prevTypeMask);
                    if (temp->isHead()) subGraph.push_back(temp);
                    m_executionSets.emplace_back();
                    break;
                }
            }

            prevTypeMask = typeMask;

            for (hcclPrim_t primToSet : subGraph)
            {
                m_executionSets.back().emplace(primToSet->type(), primToSet);
                primToSet->setExecSet(m_executionSets.size() - 1);
                LOG_HCL_DEBUG(HCL,
                              "set prim {} of type {} to exec set {}",
                              primToSet->primIdx(),
                              primToSet->type(),
                              primToSet->execSet());
            }
        }
    }
}

hcclResult_t HcclGraph::submit()
{
    setupExecSets();
    uint64_t startVal = m_graphEngine->initGraph(this);
    for (size_t j = 0; j < m_executionSets.size(); j++)
    {
        std::map<int, hcclPrim_t>& exec = m_executionSets[j];
        m_graphEngine->initExec(this, j);
        m_requestedWaits = 0;
        for (const std::pair<int, hcclPrim_t> pair : exec)
        {
            pair.second->process(m_graphEngine);
        }
        m_graphEngine->finalizeExec(this, j);
    }
    m_graphEngine->finalizeGraph(this, startVal);
    return hcclSuccess;
}

int HcclGraph::getWaits(bool incRequests)
{
    int oldVal = m_requestedWaits;
    m_requestedWaits += incRequests;
    return oldVal;
}