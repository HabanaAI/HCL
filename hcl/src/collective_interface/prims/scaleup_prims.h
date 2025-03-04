#pragma once

#include <vector>
#include "collective_interface/prims/hccl_prim.h"
#include "platform/gen2_arch_common/signals/manager.h"
#include "infra/buffer_handle_generator.h"

class IHcclGraphEngine;
class HcclGraph;

/**
 * @class HcclScaleupPrim
 * @brief A pure virtual interface for scale-up operation
 *
 * The `HcclScaleupPrim` class abstracts all the scale-up operations
 * which are collectives executed within specific server.
 */
class HcclScaleupPrim : public HcclPrim
{
public:
    HcclScaleupPrim(uint64_t sendAddr, uint64_t recvAddr, uint64_t inCnt);
    HcclScaleupPrim(uint64_t sendAddr, BufferToken recvHandle, uint64_t inCnt);

    virtual void           init(HcclGraph* graph, int idx) override;
    virtual int            type() override;
    virtual WaitEvent      getWaitEvent() override;
    virtual void           updateCounts()    = 0;
    virtual signalEvents_t getSignalEvents() = 0;

    inline uint64_t           sendAddr() const { return m_sendAddr; }
    inline uint64_t           recvAddr() const { return m_recvAddr; }
    inline uint64_t           inputCount() const { return m_inCnt; }
    inline const BufferToken& getBuffer() const { return m_recvBufferToken; }

    bool useBuffer() const;

protected:
    const uint64_t    m_sendAddr = 0;
    const uint64_t    m_recvAddr = 0;
    const BufferToken m_recvBufferToken;
    const uint64_t    m_inCnt            = 0;
    uint32_t          m_scaleupGroupSize = 0;
};

class HcclPrimAllGather : public HcclScaleupPrim
{
public:
    /**
     * @brief Constructs all gather scaleup operation primitive.
     *
     * @param sendAddr address of input data to gather.
     * @param recvAddr address of output data.
     * @param inCnt number of elements to apply.
     * @param inPlace in-place op flag
     */
    HcclPrimAllGather(uint64_t sendAddr, uint64_t recvAddr, uint64_t inCnt, bool inPlace = false);

    virtual hcclResult_t   process(IHcclGraphEngine* engine) override;
    virtual void           updateCounts() override;
    virtual signalEvents_t getSignalEvents() override;

    inline bool inPlace() const { return m_inPlace; }

private:
    const bool m_inPlace = false;
};

class HcclPrimBroadcast : public HcclScaleupPrim
{
public:
    /**
     * @brief Constructs broadcast scaleup operation primitive.
     *
     * @param sendAddr address of input data to broadcast.
     * @param recvAddr address of output data.
     * @param inCnt number of elements to apply.
     * @param isRoot root flag for broadcast op.
     */
    HcclPrimBroadcast(uint64_t sendAddr, uint64_t recvAddr, uint64_t inCnt, bool isRoot);

    virtual hcclResult_t   process(IHcclGraphEngine* engine) override;
    virtual void           updateCounts() override;
    virtual signalEvents_t getSignalEvents() override;

    inline bool isRoot() const { return m_isRoot; }

private:
    const bool m_isRoot;
};

class HcclPrimReduceScatter : public HcclScaleupPrim
{
public:
    /**
     * @brief Constructs reduce-scatter scaleup operation primitive.
     *
     * @param sendAddr address of input data to gather.
     * @param recvAddr address of output data.
     * @param inCnt number of elements to apply.
     */
    HcclPrimReduceScatter(uint64_t sendAddr, uint64_t recvAddr, uint64_t inCnt);

    /**
     * @brief Constructs reduce-scatter scaleup operation primitive.
     *
     * @param sendAddr address of input data to gather.
     * @param recvHandle token for memory buffer of output data.
     * @param inCnt number of elements to apply.
     */
    HcclPrimReduceScatter(uint64_t sendAddr, BufferToken recvHandle, uint64_t inCnt);

    virtual hcclResult_t   process(IHcclGraphEngine* engine) override;
    virtual void           init(HcclGraph* graph, int idx) override;
    virtual void           updateCounts() override;
    virtual signalEvents_t getSignalEvents() override;

    inline uint64_t getCountPerRank() { return m_scaleupCountPerRank; }

private:
    uint64_t m_scaleupCountPerRank = 0;
};