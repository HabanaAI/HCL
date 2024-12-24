#include "collective_interface/prims/simple_prims.h"
#include "collective_interface/hccl_graph.h"

HcclPrimSend::HcclPrimSend(HCL_Rank sendRank, uint64_t sendAddr, uint64_t send_cnt)
: m_sendRank(sendRank), m_sendAddr(sendAddr), m_sendCnt(send_cnt)
{
}

hcclResult_t HcclPrimSend::compile()
{
    return hcclSuccess;
}

hcclResult_t HcclPrimSend::process(IHcclGraphEngine* engine)
{
    return engine->processSendPrim(m_graph, this);
}

WaitEvent HcclPrimSend::getWaitEvent()
{
    return WaitEvent::GRAPH_SCALEOUT_SEND_WAIT_EVENT;
}

int HcclPrimSend::type()
{
    return SCALEOUT_SEND_PRIM_TYPE;
}

HcclPrimRecv::HcclPrimRecv(HCL_Rank recvRank, uint64_t recvAddr, uint64_t recv_cnt)
: m_recvRank(recvRank), m_recvAddr(recvAddr), m_recvCnt(recv_cnt)
{
}

hcclResult_t HcclPrimRecv::compile()
{
    return hcclSuccess;
}

hcclResult_t HcclPrimRecv::process(IHcclGraphEngine* engine)
{
    return engine->processRecvPrim(m_graph, this);
}

WaitEvent HcclPrimRecv::getWaitEvent()
{
    return WaitEvent::GRAPH_SCALEOUT_RECV_WAIT_EVENT;
}

int HcclPrimRecv::type()
{
    return SCALEOUT_RECV_PRIM_TYPE;
}