#pragma once

#include "collective_interface/prims/hccl_prim.h"
#include "infra/buffer_handle_generator.h"

class IHcclGraphEngine;

class HcclPrimSend : public HcclPrim
{
public:
    /**
     * @brief Constructs send data operation primitive to support transaction between devices from different servers
     * (scaleout)
     *
     * @param sendRank target rank for sending data.
     * @param sendAddr address of data to send.
     * @param sendCnt number of elements to send.
     * @param doReduction reduction flag for sent data at destination
     */
    HcclPrimSend(HCL_Rank sendRank, uint64_t sendAddr, uint64_t sendCnt, bool doReduction = false);

    /**
     * @brief Constructs send data operation primitive to support transaction between devices from different servers
     * (scaleout)
     *
     * @param sendRank target rank for sending data.
     * @param handle buffer token of data to send.
     * @param sendCnt number of elements to send.
     * @param doReduction reduction flag for sent data at destination
     */
    HcclPrimSend(HCL_Rank sendRank, BufferToken handle, uint64_t sendCnt, bool doReduction = false);

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

class HcclPrimRecv : public HcclPrim
{
public:
    /**
     * @brief Constructs receive data operation primitive to support transaction between devices from different servers
     * (scaleout)
     *
     * @param recvRank rank to receive data from.
     * @param recvAddr address of data to receive.
     * @param recvCnt number of elements to receive.
     * @param doReduction reduction flag for received data at given address
     */
    HcclPrimRecv(HCL_Rank recvRank, uint64_t recvAddr, uint64_t recvCnt, bool doReduction = false);

    /**
     * @brief Constructs receive data operation primitive to support transaction between devices from different servers
     * (scaleout)
     *
     * @param recvRank rank to receive data from.
     * @param handle buffer token of data to receive.
     * @param recvCnt number of elements to receive.
     * @param doReduction reduction flag for received data at given address
     */
    HcclPrimRecv(HCL_Rank recvRank, BufferToken handler, uint64_t recvCnt, bool doReduction = false);

    virtual hcclResult_t process(IHcclGraphEngine* engine) override;
    virtual void         init(HcclGraph* graph, int idx) override;
    virtual WaitEvent    getWaitEvent() override;
    virtual int          type() override;

    inline uint64_t           recvCount() const { return m_recvCnt; }
    inline bool               doReduction() const { return m_doReduction; }
    inline bool               useBuffer() const { return m_recvBufferToken.bufferType != INVALID_BUFFER; }
    inline const BufferToken& getBuffer() const { return m_recvBufferToken; }

    const HCL_Rank    m_recvRank = HCL_INVALID_RANK;
    const uint64_t    m_recvAddr;
    const BufferToken m_recvBufferToken;
    const uint64_t    m_recvCnt     = 0;
    const bool        m_doReduction = false;
};

class HcclPrimReduction : public HcclPrim
{
public:
    /**
     * @brief Constructs reduction operation primitive.
     *
     * @param src source address for reduction.
     * @param dst destination address for reduction.
     * @param cnt number of elements to reduce.
     */
    HcclPrimReduction(uint64_t src, uint64_t dst, uint64_t cnt);

    /**
     * @brief Constructs reduction operation primitive.
     *
     * @param srcHandle token to source memory buffer for reduction.
     * @param dst destination address for reduction.
     * @param cnt number of elements to reduce.
     */
    HcclPrimReduction(BufferToken srcHandle, uint64_t dst, uint64_t cnt);

    virtual hcclResult_t process(IHcclGraphEngine* engine) override;
    virtual void         init(HcclGraph* graph, int idx) override;
    virtual WaitEvent    getWaitEvent() override;
    virtual int          type() override;

    inline uint64_t           srcAddr() const { return m_srcAddr; }
    inline const BufferToken& getSrcBuffer() const { return m_srcBuffer; }
    inline uint64_t           dstAddr() const { return m_dstAddr; }
    inline uint64_t           cnt() const { return m_cnt; }
    inline bool               useBuffer() const { return m_srcBuffer.bufferType != INVALID_BUFFER; }

private:
    const uint64_t    m_srcAddr = 0;
    const BufferToken m_srcBuffer;
    const uint64_t    m_dstAddr = 0;
    const uint64_t    m_cnt     = 0;
};
