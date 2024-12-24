#pragma once

#include <vector>
#include "hccl_types.h"
#include "hcl_types.h"
#include "platform/gen2_arch_common/signals/types.h"

class HcclPrim;

using hcclPrim_t = std::shared_ptr<HcclPrim>;

enum PrimType
{
    SCALEUP_PRIM_TYPE       = 0,
    SCALEOUT_SEND_PRIM_TYPE = 1,
    SCALEOUT_RECV_PRIM_TYPE = 2
};

struct hcclSyncInfo
{
public:
    hcclSyncInfo() {}
    hcclSyncInfo(hcclPrim_t signaler, hcclPrim_t waiter) : m_signaler(signaler), m_waiter(waiter) {}
    bool isCrossExec();

    hcclPrim_t m_signaler = nullptr;
    hcclPrim_t m_waiter   = nullptr;
    WaitMethod syncMethod = WaitMethod::WAIT_METHOD_MAX;
};

class HcclGraph;
class IHcclGraphEngine;

class HcclPrim
{
public:
    HcclPrim()                                             = default;
    virtual ~HcclPrim()                                    = default;
    virtual hcclResult_t process(IHcclGraphEngine* engine) = 0;
    virtual hcclResult_t compile()                         = 0;
    virtual void         init(HcclGraph* graph, int idx);

    inline bool hasWait() const { return !m_waitingSyncs.empty(); }
    inline bool isHead() const { return m_waitingSyncs.empty(); }
    inline bool isSignaling() const { return !m_signalingSyncs.empty(); }
    bool        isStrongOrderRequired();

    inline int                         primIdx() { return m_primIdx; }
    inline std::vector<hcclSyncInfo*>& waiters() { return m_signalingSyncs; }
    inline std::vector<hcclSyncInfo*>& signalers() { return m_waitingSyncs; }

    virtual int       type()         = 0;
    virtual WaitEvent getWaitEvent() = 0;

    void setExecSet(int execSet);

    WaitMethod getWaitResource(bool incRequest);

    int execSet() { return m_execSet; }

protected:
    int m_primIdx = -1;
    int m_execSet = -1;

    HcclGraph* m_graph = nullptr;

    std::vector<hcclSyncInfo*> m_signalingSyncs;  // vector of sync objects represents syncs where this node is signaler
    std::vector<hcclSyncInfo*> m_waitingSyncs;    // vector of sync objects represents syncs where this node is waiter
};