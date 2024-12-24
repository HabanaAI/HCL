#include "collective_interface/collectives/all_gather.h"

#include "collective_interface/prims/hccl_prim.h"
#include "collective_interface/hccl_graph.h"
#include "collective_interface/prims/simple_prims.h"
#include "collective_interface/prims/scaleup_prims.h"

hcclResult_t agRunPairwise(IHcclGraphEngine* engine, HclCollectiveParams& params)
{
    const HCL_Rank myRank           = params.m_dynamicComm.getMyRank();
    const uint16_t myBox            = params.m_dynamicComm.getMyScaleupGroup();
    const uint32_t commSize         = params.m_dynamicComm.getCommSize();
    const uint32_t scaleupGroupSize = params.m_dynamicComm.getScaleupGroupSize();
    const uint32_t boxCount         = commSize / scaleupGroupSize;
    const uint64_t inputCount       = params.m_count;
    const uint64_t offset           = inputCount * dataTypeSizeInBytes(params.m_dataType);

    HcclGraph graph(engine, &params);
    graph.createPrim<HcclPrimAllGather>(params.m_sendBufferAddr,
                                        params.m_recvBufferAddr + myBox * scaleupGroupSize * offset,
                                        inputCount);

    for (uint32_t i = 1; i < boxCount; i++)
    {
        HCL_Rank sendRank = (myRank + i * scaleupGroupSize) % commSize;
        HCL_Rank recvRank = (myRank - i * scaleupGroupSize + commSize) % commSize;

        uint64_t soRecvAddr = params.m_recvBufferAddr + recvRank * offset;
        uint16_t recvBox    = params.m_dynamicComm.getRankToScaleupGroupMap()[recvRank];
        uint64_t boxOffset  = recvBox * scaleupGroupSize * offset;
        uint64_t agOutAddr  = params.m_recvBufferAddr + boxOffset;

        auto soRecv = graph.createPrim<HcclPrimRecv>(recvRank, soRecvAddr, inputCount);
        auto ag     = graph.createPrim<HcclPrimAllGather>(soRecvAddr, agOutAddr, inputCount);
        graph.addWait(soRecv, ag);

        graph.createPrim<HcclPrimSend>(sendRank, params.m_sendBufferAddr, inputCount);
    }
    return graph.submit();
}

hcclResult_t agRunRing(IHcclGraphEngine* engine, HclCollectiveParams& params)
{
    const HCL_Rank myRank           = params.m_dynamicComm.getMyRank();
    const uint16_t myBox            = params.m_dynamicComm.getMyScaleupGroup();
    const uint32_t commSize         = params.m_dynamicComm.getCommSize();
    const uint32_t scaleupGroupSize = params.m_dynamicComm.getScaleupGroupSize();
    const uint32_t boxCount         = commSize / scaleupGroupSize;
    const uint64_t inputCount       = params.m_count;
    const uint64_t offset           = inputCount * dataTypeSizeInBytes(params.m_dataType);

    HcclGraph graph(engine, &params);

    const HCL_Rank sendRank = (myRank + scaleupGroupSize) % commSize;
    const HCL_Rank recvRank = (myRank - scaleupGroupSize + commSize) % commSize;

    auto ag = graph.createPrim<HcclPrimAllGather>(params.m_sendBufferAddr,
                                                  params.m_recvBufferAddr + myBox * scaleupGroupSize * offset,
                                                  inputCount);
    if (boxCount > 1)
    {
        auto soSend = graph.createPrim<HcclPrimSend>(sendRank,
                                                     params.m_sendBufferAddr,
                                                     inputCount);  // first iteration, send from input
        graph.addWait(ag, soSend);
    }

    for (uint32_t i = 1; i < boxCount; i++)
    {
        uint64_t soRecvAddr =
            params.m_recvBufferAddr + ((myRank - scaleupGroupSize * i + commSize) % commSize) * offset;
        uint64_t agSendAddr = soRecvAddr;
        uint64_t agRecvAddr = agSendAddr - params.m_dynamicComm.getRankInScaleupGroup() * offset;

        auto soRecv = graph.createPrim<HcclPrimRecv>(recvRank, soRecvAddr, inputCount);
        ag          = graph.createPrim<HcclPrimAllGather>(agSendAddr, agRecvAddr, inputCount);

        graph.addWait(soRecv, ag);

        if (i < boxCount - 1)  // last iter ->  scaleup + scaleout recv only
        {
            auto soSend = graph.createPrim<HcclPrimSend>(sendRank, soRecvAddr, inputCount);
            graph.addWait(soRecv, soSend);
        }
    }
    return graph.submit();
}