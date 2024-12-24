#pragma once

#include "collective_interface/prims/hccl_prim.h"

class IHcclGraphEngine;

class HcclPrimSend : public HcclPrim
{
public:
    HcclPrimSend(HCL_Rank sendRank, uint64_t sendAddr, uint64_t send_cnt);

    virtual hcclResult_t compile() override;
    virtual hcclResult_t process(IHcclGraphEngine* engine) override;
    virtual WaitEvent    getWaitEvent() override;
    virtual int          type() override;

    inline uint64_t sendCount() const { return m_sendCnt; }

    const HCL_Rank m_sendRank = HCL_INVALID_RANK;
    const uint64_t m_sendAddr;
    const uint64_t m_sendCnt = 0;
};

class HcclPrimRecv : public HcclPrim
{
public:
    HcclPrimRecv(HCL_Rank recvRank, uint64_t recvAddr, uint64_t recv_cnt);

    virtual hcclResult_t compile() override;
    virtual hcclResult_t process(IHcclGraphEngine* engine) override;
    virtual WaitEvent    getWaitEvent() override;
    virtual int          type() override;

    inline uint64_t recvCount() const { return m_recvCnt; }

    const HCL_Rank m_recvRank = HCL_INVALID_RANK;
    const uint64_t m_recvAddr;
    const uint64_t m_recvCnt = 0;
};
