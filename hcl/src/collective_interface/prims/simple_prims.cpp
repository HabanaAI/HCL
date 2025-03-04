#include "collective_interface/prims/simple_prims.h"
#include "collective_interface/hccl_graph.h"

HcclPrimSend::HcclPrimSend(HCL_Rank sendRank, uint64_t sendAddr, uint64_t sendCnt, bool doReduction)
: m_sendRank(sendRank), m_sendAddr(sendAddr), m_sendCnt(sendCnt), m_doReduction(doReduction)
{
}

HcclPrimSend::HcclPrimSend(HCL_Rank sendRank, BufferToken handle, uint64_t sendCnt, bool doReduction)
: m_sendRank(sendRank), m_sendAddr(0), m_sendBufferToken(handle), m_sendCnt(sendCnt), m_doReduction(doReduction)
{
}

void HcclPrimSend::init(HcclGraph* graph, int idx)
{
    HcclPrim::init(graph, idx);
    m_graph->verifyHandle(getBuffer());
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

HcclPrimRecv::HcclPrimRecv(HCL_Rank recvRank, uint64_t recvAddr, uint64_t recvCnt, bool doReduction)
: m_recvRank(recvRank), m_recvAddr(recvAddr), m_recvCnt(recvCnt), m_doReduction(doReduction)
{
}

HcclPrimRecv::HcclPrimRecv(HCL_Rank recvRank, BufferToken handle, uint64_t recvCnt, bool doReduction)
: m_recvRank(recvRank), m_recvAddr(0), m_recvBufferToken(handle), m_recvCnt(recvCnt), m_doReduction(doReduction)
{
}

void HcclPrimRecv::init(HcclGraph* graph, int idx)
{
    HcclPrim::init(graph, idx);
    m_graph->verifyHandle(getBuffer());
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

HcclPrimReduction::HcclPrimReduction(uint64_t src, uint64_t dst, uint64_t cnt)
: m_srcAddr(src), m_dstAddr(dst), m_cnt(cnt)
{
}

HcclPrimReduction::HcclPrimReduction(BufferToken srcHandle, uint64_t dst, uint64_t cnt)
: m_srcBuffer(srcHandle), m_dstAddr(dst), m_cnt(cnt)
{
}

void HcclPrimReduction::init(HcclGraph* graph, int idx)
{
    HcclPrim::init(graph, idx);
    m_graph->verifyHandle(getSrcBuffer());
}

hcclResult_t HcclPrimReduction::process(IHcclGraphEngine* engine)
{
    return engine->processReductionPrim(m_graph, this);
}

WaitEvent HcclPrimReduction::getWaitEvent()
{
    return WaitEvent::GRAPH_REDUCTION_WAIT_EVENT;
}

int HcclPrimReduction::type()
{
    return REDUCTION_PRIM_TYPE;
}
