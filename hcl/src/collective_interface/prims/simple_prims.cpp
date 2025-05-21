#include "collective_interface/prims/simple_prims.h"
#include "collective_interface/hccl_graph.h"

HcclPrimSend::HcclPrimSend(SendPrimArgs& args)
: m_sendRank(args.sendRank),
  m_sendAddr(args.sendAddr),
  m_sendBufferToken(args.sendBufferToken),
  m_sendCnt(args.sendCnt),
  m_doReduction(args.doReduction)
{
}

HcclPrimSend::HcclPrimSend(SendPrimArgs&& args)
: m_sendRank(args.sendRank),
  m_sendAddr(args.sendAddr),
  m_sendBufferToken(args.sendBufferToken),
  m_sendCnt(args.sendCnt),
  m_doReduction(args.doReduction)
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

HcclPrimRecv::HcclPrimRecv(RecvPrimArgs& args)
: m_recvRank(args.recvRank),
  m_recvAddr(args.recvAddr),
  m_recvBufferToken(args.recvBufferToken),
  m_recvCnt(args.recvCnt),
  m_doReduction(args.doReduction),
  m_castUp(args.castUp)
{
    VERIFY((args.recvAddr != 0) ^ (args.recvBufferToken.bufferType != INVALID_BUFFER),
           "Can use exactly one addressing method (address/buffer)");
}

HcclPrimRecv::HcclPrimRecv(RecvPrimArgs&& args)
: m_recvRank(args.recvRank),
  m_recvAddr(args.recvAddr),
  m_recvBufferToken(args.recvBufferToken),
  m_recvCnt(args.recvCnt),
  m_doReduction(args.doReduction),
  m_castUp(args.castUp)
{
    VERIFY((args.recvAddr != 0) ^ (args.recvBufferToken.bufferType != INVALID_BUFFER),
           "Can use exactly one addressing method (address/buffer)");
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

HcclPrimReduction::HcclPrimReduction(ReductionPrimArgs& args)
: m_srcAddr(args.srcAddr),
  m_srcBuffer(args.srcBuffer),
  m_dstAddr(args.dstAddr),
  m_cnt(args.cnt),
  m_castDown(args.castDown)
{
}

HcclPrimReduction::HcclPrimReduction(ReductionPrimArgs&& args)
: m_srcAddr(args.srcAddr),
  m_srcBuffer(args.srcBuffer),
  m_dstAddr(args.dstAddr),
  m_cnt(args.cnt),
  m_castDown(args.castDown)
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
