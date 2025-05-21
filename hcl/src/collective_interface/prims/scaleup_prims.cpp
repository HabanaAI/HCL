#include "collective_interface/prims/scaleup_prims.h"
#include "collective_interface/hccl_graph.h"
#include "hcl_math_utils.h"

HcclScaleupPrim::HcclScaleupPrim(ScaleupPrimArgs& args)
: m_sendAddr(args.sendAddr), m_recvAddr(args.recvAddr), m_recvBufferToken(args.recvHandle), m_inCnt(args.inCnt)
{
}

HcclScaleupPrim::HcclScaleupPrim(ScaleupPrimArgs&& args)
: m_sendAddr(args.sendAddr), m_recvAddr(args.recvAddr), m_recvBufferToken(args.recvHandle), m_inCnt(args.inCnt)
{
}

HcclScaleupPrim::HcclScaleupPrim(uint64_t sendAddr, uint64_t recvAddr, uint64_t inCnt)
: m_sendAddr(sendAddr), m_recvAddr(recvAddr), m_inCnt(inCnt)
{
}

HcclScaleupPrim::HcclScaleupPrim(uint64_t sendAddr, BufferToken recvHandle, uint64_t inCnt)
: m_sendAddr(sendAddr), m_recvBufferToken(recvHandle), m_inCnt(inCnt)
{
}

void HcclScaleupPrim::init(HcclGraph* graph, int idx)
{
    HcclPrim::init(graph, idx);
    m_graph->verifyHandle(getBuffer());
    m_scaleupGroupSize = m_graph->graphParams()->m_dynamicComm.getScaleupGroupSize();
}

int HcclScaleupPrim::type()
{
    return SCALEUP_PRIM_TYPE;
}

WaitEvent HcclScaleupPrim::getWaitEvent()
{
    return WaitEvent::GRAPH_SCALEUP_WAIT_EVENT;
}

bool HcclScaleupPrim::useBuffer() const
{
    return m_recvBufferToken.bufferType != INVALID_BUFFER;
}

HcclPrimAllGather::HcclPrimAllGather(uint64_t sendAddr, uint64_t recvAddr, uint64_t inCnt, bool inPlace)
: HcclScaleupPrim(sendAddr, recvAddr, inCnt), m_inPlace(inPlace)
{
}

hcclResult_t HcclPrimAllGather::process(IHcclGraphEngine* engine)
{
    return engine->processAgPrim(m_graph, this);
}

signalEvents_t HcclPrimAllGather::getSignalEvents()
{
    const auto&    commonState = *(m_graph->graphState());
    const HCL_Rank rank        = commonState.m_dynamicComm.getRankInScaleupGroup();
    const uint64_t dataSize    = inputCount() * commonState.m_dataTypeSizeInBytes;
    const bool     opInPlace   = sendAddr() == (recvAddr() + rank * dataSize) || sendAddr() == recvAddr() || inPlace();

    signalEvents_t signalEvents = {SignalEvent::SCALEUP_SEND, SignalEvent::SCALEUP_RECV};

    if (!opInPlace) signalEvents.push_back(SignalEvent::EDMA_MEMCOPY);
    return signalEvents;
}

void HcclPrimAllGather::updateCounts()
{
    m_graph->sendSlice().m_rankScaleUpCount = m_graph->recvSlice().m_rankScaleUpCount = inputCount();
    m_graph->sendSlice().m_scaleUpStrideCount = m_graph->recvSlice().m_scaleUpStrideCount = inputCount();
    m_graph->sendSlice().m_boxCount = m_graph->recvSlice().m_boxCount = (inputCount() * m_scaleupGroupSize);
    m_graph->sendSlice().m_boxStrideCount = m_graph->recvSlice().m_boxStrideCount = (inputCount() * m_scaleupGroupSize);
}

HcclPrimBroadcast::HcclPrimBroadcast(uint64_t sendAddr, uint64_t recvAddr, uint64_t inCnt, bool isRoot)
: HcclScaleupPrim(sendAddr, recvAddr, inCnt), m_isRoot(isRoot)
{
}

hcclResult_t HcclPrimBroadcast::process(IHcclGraphEngine* engine)
{
    return engine->processBcastPrim(m_graph, this);
}

signalEvents_t HcclPrimBroadcast::getSignalEvents()
{
    const bool     opInPlace    = sendAddr() == recvAddr();
    signalEvents_t signalEvents = {};
    if (!isRoot())
    {
        signalEvents.push_back(SignalEvent::SCALEUP_RECV);
    }
    else
    {
        signalEvents.push_back(SignalEvent::SCALEUP_SEND);
        if (!opInPlace) signalEvents.push_back(SignalEvent::EDMA_MEMCOPY);
    }
    return signalEvents;
}

void HcclPrimBroadcast::updateCounts()
{
    m_graph->sendSlice().m_rankScaleUpCount = m_graph->recvSlice().m_rankScaleUpCount = inputCount();
    m_graph->sendSlice().m_scaleUpStrideCount = m_graph->recvSlice().m_scaleUpStrideCount = inputCount();
    m_graph->sendSlice().m_boxCount = m_graph->recvSlice().m_boxCount = (inputCount() * m_scaleupGroupSize);
}

HcclPrimReduceScatter::HcclPrimReduceScatter(ReduceScatterPrimArgs& args)
: HcclScaleupPrim(args.scaleupArg), m_castUp(args.castUp)
{
}

HcclPrimReduceScatter::HcclPrimReduceScatter(ReduceScatterPrimArgs&& args)
: HcclScaleupPrim(args.scaleupArg), m_castUp(args.castUp)
{
}

hcclResult_t HcclPrimReduceScatter::process(IHcclGraphEngine* engine)
{
    return engine->processRsPrim(m_graph, this);
}

signalEvents_t HcclPrimReduceScatter::getSignalEvents()
{
    return {SignalEvent::EDMA_BATCH};
}

void HcclPrimReduceScatter::init(HcclGraph* graph, int idx)
{
    HcclScaleupPrim::init(graph, idx);
    m_scaleupCountPerRank = div(inputCount(), (uint64_t)m_scaleupGroupSize);
}

void HcclPrimReduceScatter::updateCounts()
{
    m_graph->sendSlice().m_rankScaleOutCount = m_scaleupCountPerRank;  // for edma chunk size
    m_graph->sendSlice().m_rankScaleUpCount = m_graph->recvSlice().m_rankScaleUpCount = m_scaleupCountPerRank;
    m_graph->sendSlice().m_scaleUpStrideCount = m_graph->recvSlice().m_scaleUpStrideCount = m_scaleupCountPerRank;
    m_graph->sendSlice().m_boxCount = m_graph->recvSlice().m_boxCount = inputCount();
}
