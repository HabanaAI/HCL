
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
    m_completionSets.emplace_back();
    uint8_t prevTypeMask = 0, typeMask = 0;
    for (hcclPrim_t extractedPrim : m_prims)
    {
        typeMask = 0;
        if (extractedPrim->execSet() >= 0) continue;
        else
        {
            std::vector<hcclPrim_t> primsToSet;
            bool                    allocatedNewSet = false;
            std::vector<hcclPrim_t> subGraph        = {extractedPrim};

            for (hcclSyncInfo* sync : extractedPrim->waiters())
            {
                subGraph.push_back(sync->m_waiter);
            }

            for (hcclPrim_t prim : subGraph)
            {
                if (((typeMask | prevTypeMask) & (1 << prim->type())) == 0)
                {
                    typeMask |= (1 << prim->type());
                    primsToSet.push_back(prim);
                }
                else
                {
                    if (!allocatedNewSet)
                    {
                        m_completionSets.emplace_back();
                        allocatedNewSet = true;
                        prevTypeMask    = 0;
                    }
                    if (((typeMask & (1 << prim->type())) == 0))
                    {
                        typeMask |= (1 << prim->type());
                        primsToSet.push_back(prim);
                    }
                }
            }

            prevTypeMask |= typeMask;

            for (hcclPrim_t primToSet : primsToSet)
            {
                m_completionSets.back().prims.push_back(primToSet);
                primToSet->setExecSet(m_completionSets.size() - 1);
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
    for (size_t j = 0; j < m_completionSets.size(); j++)
    {
        PrimCompletionDescriptor exec = m_completionSets[j];
        m_graphEngine->initExec(this, j);
        m_requestedWaits = 0;
        for (hcclPrim_t prim : exec.prims)
        {
            prim->process(m_graphEngine);
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

BufferToken HcclGraph::generateBufferToken(BufferType type)
{
    return bufferGenerator.generateBufferToken(type);
}

bool HcclGraph::checkTypeAllocation(BufferType type)
{
    return bufferGenerator.checkTypeAllocation(type);
}

void HcclGraph::verifyHandle(const BufferToken& handle)
{
    return bufferGenerator.verifyHandle(handle);
}