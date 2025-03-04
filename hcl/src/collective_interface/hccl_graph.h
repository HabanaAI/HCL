#pragma once

#include <set>
#include "hccl_types.h"
#include "hcl_collective_params.h"
#include "platform/gen2_arch_common/collective_states.h"
#include "infra/buffer_handle_generator.h"
#include "collective_interface/prims/hccl_prim.h"

class HcclPrim;
class HcclPrimAllGather;
class HcclPrimBroadcast;
class HcclPrimReduceScatter;
class HcclPrimSend;
class HcclPrimRecv;
class HcclPrimReduction;
class HcclGraph;
class BufferTokenGenerator;

class IHcclGraphEngine
{
public:
    virtual ~IHcclGraphEngine() noexcept(false) {};
    virtual uint64_t     initGraph(HcclGraph* graph)                                              = 0;
    virtual void         finalizeGraph(HcclGraph* graph, uint64_t startTargetVal)                 = 0;
    virtual void         initExec(HcclGraph* graph, int exec)                                     = 0;
    virtual void         finalizeExec(HcclGraph* graph, int exec)                                 = 0;
    virtual hcclResult_t processAgPrim(HcclGraph* graph, HcclPrimAllGather* agPrim)               = 0;
    virtual hcclResult_t processBcastPrim(HcclGraph* graph, HcclPrimBroadcast* bcastPrim)         = 0;
    virtual hcclResult_t processRsPrim(HcclGraph* graph, HcclPrimReduceScatter* rsPrim)           = 0;
    virtual hcclResult_t processSendPrim(HcclGraph* graph, HcclPrimSend* sendPrim)                = 0;
    virtual hcclResult_t processRecvPrim(HcclGraph* graph, HcclPrimRecv* recvPrim)                = 0;
    virtual hcclResult_t processReductionPrim(HcclGraph* graph, HcclPrimReduction* reductionPrim) = 0;
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

struct CompletionResources
{
    bool longtermSo = false;
    bool tempBuff   = false;
};

struct PrimCompletionDescriptor
{
    std::vector<hcclPrim_t> prims;
    CompletionResources     requiredResources;
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

    /**
     * @brief Submits all stored primitives for execution
     *
     * @return hcclResult_t enum type for submission result
     */
    hcclResult_t submit();

    /**
     * @brief Create primitive of generic type
     *
     * @tparam T type of primitive
     * @tparam Args list of primitive constructor arguments
     * @return std::shared_ptr<T> shared pointer to created primitive stored in internal container
     */
    template<typename T, typename... Args>
    std::shared_ptr<T> createPrim(Args&&... args)
    {
        auto prim = std::make_shared<T>(std::forward<Args>(args)...);
        m_prims.emplace_back(prim);
        prim->init(this, m_primCtr++);
        return prim;
    }

    /**
     * @brief Adds wait relation between 2 primitives
     *
     * @param signaler hcclPrim_t which its completion releases waiting primitive
     * @param waiter hcclPrim_t which waits for signaler to complete prior to its own execution
     */

    virtual void addWait(hcclPrim_t signaler, hcclPrim_t waiter);

    /**
     * @brief Requests buffer usage token to be consumed by primitive. Token is later evaluated to a pre-allocated
     * memory buffer by IHcclGraphEngine internally
     * @param type BufferType enum which defines lifetime of requested buffer type
     * STATIC_BUFFER - will request buffer that can be used throughout primitive graph (single buffer allowed)
     * TEMP_BUFFER - will request buffer that can be used until next call to generateBufferToken with this type
     * (unlimited buffers)
     * @return BufferToken to be as input by primitives
     */
    BufferToken generateBufferToken(BufferType type);

    bool checkTypeAllocation(BufferType type);

    void verifyHandle(const BufferToken& handle);

    int getWaits(bool incRequests);

    inline HclCollectiveParams*            graphParams() { return m_params; }
    inline std::shared_ptr<CommonState>&   graphState() { return context().m_state; }
    inline HcclGraphContext&               context() { return m_graphContext; }
    inline SliceState&                     sendSlice() { return *context().m_sendSliceState; }
    inline SliceState&                     recvSlice() { return *context().m_recvSliceState; }
    inline size_t                          totalExecSets() { return m_completionSets.size(); }
    inline std::deque<hcclSyncInfo>&       syncs() { return m_syncs; }
    std::vector<PrimCompletionDescriptor>& completionSets() { return m_completionSets; }

    bool startStrongOrder = false;

protected:
    void setupExecSets();

private:
    IHcclGraphEngine*    m_graphEngine = nullptr;
    HclCollectiveParams* m_params      = nullptr;
    HcclGraphContext     m_graphContext;

    std::vector<hcclPrim_t>               m_prims;
    std::deque<hcclSyncInfo>              m_syncs;
    std::vector<PrimCompletionDescriptor> m_completionSets;

    BufferTokenGenerator bufferGenerator;

    int m_primCtr        = 0;
    int m_requestedWaits = 0;
};