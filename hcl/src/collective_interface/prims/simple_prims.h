#pragma once

#include "collective_interface/prims/hccl_prim.h"
#include "infra/buffer_handle_generator.h"

class IHcclGraphEngine;

struct SendPrimArgs
{
    HCL_Rank    sendRank = HCL_INVALID_RANK;  // target rank for sending data.
    uint64_t    sendAddr;                     // address of data to send.
    BufferToken sendBufferToken;              // buffer token of data to send.
    uint64_t    sendCnt     = 0;              // number of elements to send.
    bool        doReduction = false;          // reduction flag for sent data at destination
};

class HcclPrimSend : public HcclPrim
{
public:
    /**
     * @brief Constructs send data operation primitive to support transaction between devices from different servers
     * (scaleout)
     *
     * @param args send arguments struct
     */
    HcclPrimSend(SendPrimArgs& args);
    HcclPrimSend(SendPrimArgs&& args);

    virtual hcclResult_t process(IHcclGraphEngine* engine) override;
    virtual void         init(HcclGraph* graph, int idx) override;
    virtual WaitEvent    getWaitEvent() override;
    virtual int          type() override;

    inline uint64_t           sendCount() const { return m_sendCnt; }
    inline bool               doReduction() const { return m_doReduction; }
    inline bool               useBuffer() const { return m_sendBufferToken.bufferType != INVALID_BUFFER; }
    inline const BufferToken& getBuffer() const { return m_sendBufferToken; }

    const HCL_Rank    m_sendRank = HCL_INVALID_RANK;
    const uint64_t    m_sendAddr;
    const BufferToken m_sendBufferToken;
    const uint64_t    m_sendCnt     = 0;
    const bool        m_doReduction = false;
};

struct RecvPrimArgs
{
    HCL_Rank    recvRank = HCL_INVALID_RANK;  // rank to receive data from.
    uint64_t    recvAddr;                     // address of data to receive.
    BufferToken recvBufferToken;              // buffer token of data to receive.
    uint64_t    recvCnt     = 0;              // number of elements to receive.
    bool        doReduction = false;          // reduction flag for received data at given address
    bool        castUp      = false;          // cast up flag for received data at given address
};

class HcclPrimRecv : public HcclPrim
{
public:
    /**
     * @brief Constructs receive data operation primitive to support transaction between devices from different servers
     * (scaleout)
     *
     * @param args recv arguments struct
     */
    HcclPrimRecv(RecvPrimArgs& args);
    HcclPrimRecv(RecvPrimArgs&& args);

    virtual hcclResult_t process(IHcclGraphEngine* engine) override;
    virtual void         init(HcclGraph* graph, int idx) override;
    virtual WaitEvent    getWaitEvent() override;
    virtual int          type() override;

    inline uint64_t           recvCount() const { return m_recvCnt; }
    inline bool               doReduction() const { return m_doReduction; }
    inline bool               castUp() const { return m_castUp; }
    inline bool               useBuffer() const { return m_recvBufferToken.bufferType != INVALID_BUFFER; }
    inline const BufferToken& getBuffer() const { return m_recvBufferToken; }

    const HCL_Rank    m_recvRank = HCL_INVALID_RANK;
    const uint64_t    m_recvAddr;
    const BufferToken m_recvBufferToken;
    const uint64_t    m_recvCnt     = 0;
    const bool        m_doReduction = false;
    const bool        m_castUp      = false;
};

struct ReductionPrimArgs
{
    uint64_t    srcAddr = 0;       // source address for reduction.
    BufferToken srcBuffer;         // token to source memory buffer for reduction.
    uint64_t    dstAddr  = 0;      // destination address for reduction.
    uint64_t    cnt      = 0;      // number of elements to reduce.
    bool        castDown = false;  // cast down reduction result flag
};

class HcclPrimReduction : public HcclPrim
{
public:
    /**
     * @brief Constructs reduction operation primitive.
     *
     * @param arg parameters for reduction operation.
     */
    HcclPrimReduction(ReductionPrimArgs& args);
    HcclPrimReduction(ReductionPrimArgs&& args);

    virtual hcclResult_t process(IHcclGraphEngine* engine) override;
    virtual void         init(HcclGraph* graph, int idx) override;
    virtual WaitEvent    getWaitEvent() override;
    virtual int          type() override;

    inline uint64_t           srcAddr() const { return m_srcAddr; }
    inline const BufferToken& getSrcBuffer() const { return m_srcBuffer; }
    inline uint64_t           dstAddr() const { return m_dstAddr; }
    inline uint64_t           cnt() const { return m_cnt; }
    inline bool               useBuffer() const { return m_srcBuffer.bufferType != INVALID_BUFFER; }
    inline bool               castDown() const { return m_castDown; }

private:
    const uint64_t    m_srcAddr = 0;
    const BufferToken m_srcBuffer;
    const uint64_t    m_dstAddr  = 0;
    const uint64_t    m_cnt      = 0;
    const bool        m_castDown = 0;
};
