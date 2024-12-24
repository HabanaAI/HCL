#pragma once

#include <vector>
#include "collective_interface/prims/hccl_prim.h"
#include "platform/gen2_arch_common/signals/manager.h"

typedef llvm_vecsmall::SmallVector<SignalsManager::SignalDescription, 8> signalEvents_t;

static constexpr uint64_t SCALEUP_BUFF  = 0xffffffffffffffff - 1;
static constexpr uint64_t SCALEOUT_BUFF = 0xffffffffffffffff;

class IHcclGraphEngine;
class HcclGraph;

class HcclScaleupPrim : public HcclPrim
{
public:
    HcclScaleupPrim(uint64_t sendAddr, uint64_t recvAddr, uint64_t inCnt);

    virtual void      init(HcclGraph* graph, int idx) override;
    virtual int       type() override;
    virtual WaitEvent getWaitEvent() override;
    virtual void      updateCounts() = 0;

    inline uint64_t sendAddr() const { return m_sendAddr; }
    inline uint64_t recvAddr() const { return m_recvAddr; }
    inline uint64_t inputCount() const { return m_inCnt; }

protected:
    const uint64_t m_sendAddr         = 0;
    const uint64_t m_recvAddr         = 0;
    const uint64_t m_inCnt            = 0;
    uint32_t       m_scaleupGroupSize = 0;
};

class HcclPrimAllGather : public HcclScaleupPrim
{
public:
    HcclPrimAllGather(uint64_t sendAddr, uint64_t recvAddr, uint64_t inCnt, bool inPlace = false);

    virtual hcclResult_t compile() override;
    virtual hcclResult_t process(IHcclGraphEngine* engine) override;
    virtual void         updateCounts() override;

    inline bool    inPlace() const { return m_inPlace; }
    signalEvents_t getSignalEvents();

private:
    const bool m_inPlace = false;
};

class HcclPrimBroadcast : public HcclScaleupPrim
{
public:
    HcclPrimBroadcast(uint64_t sendAddr, uint64_t recvAddr, uint64_t inCnt, bool isRoot);

    virtual hcclResult_t compile() override;
    virtual hcclResult_t process(IHcclGraphEngine* engine) override;
    virtual void         updateCounts() override;
    inline bool          isRoot() const { return m_isRoot; }
    signalEvents_t       getSignalEvents();

private:
    const bool m_isRoot;
};