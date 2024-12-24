#pragma once

#include <set>
#include "hccl_types.h"
#include "hcl_collective_params.h"
#include "platform/gen2_arch_common/collective_states.h"
#include "collective_interface/prims/hccl_prim.h"

class HcclPrim;
class HcclPrimAllGather;
class HcclPrimBroadcast;
class HcclPrimSend;
class HcclPrimRecv;
class HcclGraph;

class IHcclGraphEngine
{
public:
    virtual ~IHcclGraphEngine() noexcept(false) {};
    virtual uint64_t     initGraph(HcclGraph* graph)                               = 0;
    virtual void         finalizeGraph(HcclGraph* graph, uint64_t startTargetVal)  = 0;
    virtual void         initExec(HcclGraph* graph, int exec)                      = 0;
    virtual void         finalizeExec(HcclGraph* graph, int exec)                  = 0;
    virtual hcclResult_t processAgPrim(HcclGraph* graph, HcclPrimAllGather* agPrim)       = 0;
    virtual hcclResult_t processBcastPrim(HcclGraph* graph, HcclPrimBroadcast* bcastPrim) = 0;
    virtual hcclResult_t processSendPrim(HcclGraph* graph, HcclPrimSend* sendPrim) = 0;
    virtual hcclResult_t processRecvPrim(HcclGraph* graph, HcclPrimRecv* recvPrim) = 0;
};

struct HcclGraphContext
{
    HcclGraphContext()          = default;
    virtual ~HcclGraphContext() = default;

    BoxNumInfo                   m_myBoxNumInfo = {0xffffffff, BoxNumInfo::boxOrientation::PREV_BOX};
    std::shared_ptr<CommonState> m_state;
    std::shared_ptr<SliceState>  m_sendSliceState;
    std::shared_ptr<SliceState>  m_recvSliceState;
};

class HcclGraph
{
public:
    HcclGraph(IHcclGraphEngine* engine, HclCollectiveParams* params);
    virtual ~HcclGraph()                   = default;
    HcclGraph(HcclGraph&&)                 = delete;
    HcclGraph(const HcclGraph&)            = delete;
    HcclGraph& operator=(HcclGraph&&)      = delete;
    HcclGraph& operator=(const HcclGraph&) = delete;

    hcclResult_t compile() { return hcclSuccess; }
    hcclResult_t submit();

    template<typename T, typename... Args>
    std::shared_ptr<T> createPrim(Args&&... args)
    {
        auto prim = std::make_shared<T>(std::forward<Args>(args)...);
        m_prims.emplace_back(prim);
        prim->init(this, m_primCtr++);
        return prim;
    }

    virtual void addWait(hcclPrim_t signaler, hcclPrim_t waiter);

    void setupExecSets();

    int getWaitsAndInc() { return m_requestedWaits++; }
    int getWaits(bool incRequests);

    void incWaits() { m_requestedWaits++; }

    inline HclCollectiveParams*         graphParams() { return m_params; }
    inline std::shared_ptr<CommonState>& graphState() { return context().m_state; }
    inline HcclGraphContext&             context() { return m_graphContext; }
    inline SliceState&                   sendSlice() { return *context().m_sendSliceState; }
    inline SliceState&                   recvSlice() { return *context().m_recvSliceState; }
    inline std::deque<hcclSyncInfo>&     syncs() { return m_syncs; }

    bool startStrongOrder = false;

private:
    IHcclGraphEngine*            m_graphEngine = nullptr;
    HclCollectiveParams*         m_params      = nullptr;
    HcclGraphContext             m_graphContext;

    std::vector<hcclPrim_t>                m_prims;
    std::deque<hcclSyncInfo>               m_syncs;
    std::vector<std::map<int, hcclPrim_t>> m_executionSets;  // key is primitive type()

    int      m_primCtr             = 0;
    int      m_requestedWaits      = 0;
};