#include "collective_interface/collectives/all_reduce.h"

#include "collective_interface/prims/hccl_prim.h"
#include "collective_interface/hccl_graph.h"
#include "collective_interface/prims/simple_prims.h"
#include "collective_interface/prims/scaleup_prims.h"

hcclResult_t ar_runPairwise(IHcclGraphEngine* engine, HclCollectiveParams& params)
{
    const HCL_Rank myRank           = params.m_dynamicComm.getMyRank();
    const uint16_t myBox            = params.m_dynamicComm.getMyScaleupGroup();
    const uint32_t commSize         = params.m_dynamicComm.getCommSize();
    const uint32_t scaleupGroupSize = params.m_dynamicComm.getScaleupGroupSize();
    const uint32_t boxesCount       = commSize / scaleupGroupSize;
    const uint64_t countPerRank     = params.m_count / commSize;
    const uint64_t rankAddrOffset   = countPerRank * dataTypeSizeInBytes(params.m_dataType);

    {
        params.m_currentOp = eHCLReduceScatter;
        HcclGraph graph(engine, &params);

        BufferToken scaleoutBuff = graph.generateBufferToken(STATIC_BUFFER);

        if (boxesCount > 1)
        {
            graph.createPrim<HcclPrimReduceScatter>(params.m_sendBufferAddr + myBox * scaleupGroupSize * rankAddrOffset,
                                                    scaleoutBuff,
                                                    countPerRank * scaleupGroupSize);
        }
        else
        {
            graph.createPrim<HcclPrimReduceScatter>(params.m_sendBufferAddr + myBox * scaleupGroupSize * rankAddrOffset,
                                                    params.m_recvBufferAddr +
                                                        params.m_dynamicComm.getRankInScaleupGroup() * rankAddrOffset,
                                                    countPerRank * scaleupGroupSize);
        }

        for (uint32_t i = 1; i < boxesCount; i++)
        {
            HCL_Rank sendRank = (myRank + i * scaleupGroupSize) % commSize;
            HCL_Rank recvRank = (myRank - i * scaleupGroupSize + commSize) % commSize;

            uint16_t sendBox     = params.m_dynamicComm.getRankToScaleupGroupMap()[sendRank];
            uint64_t boxOffset   = sendBox * scaleupGroupSize * rankAddrOffset;
            uint64_t rsInputAddr = params.m_sendBufferAddr + boxOffset;

            BufferToken scaleupBuff = graph.generateBufferToken(TEMP_BUFFER);

            auto rs =
                graph.createPrim<HcclPrimReduceScatter>(rsInputAddr, scaleupBuff, countPerRank * scaleupGroupSize);

            const bool doReduction = true;

            auto send = graph.createPrim<HcclPrimSend>(sendRank, scaleupBuff, countPerRank, doReduction);

            graph.addWait(rs, send);

            auto recv = graph.createPrim<HcclPrimRecv>(recvRank, scaleoutBuff, countPerRank, doReduction);

            if (i == boxesCount - 1)
            {
                uint64_t reductionDstAddr = params.m_recvBufferAddr + myBox * scaleupGroupSize * rankAddrOffset +
                                            params.m_dynamicComm.getRankInScaleupGroup() * rankAddrOffset;
                auto reduction = graph.createPrim<HcclPrimReduction>(scaleoutBuff, reductionDstAddr, countPerRank);
                graph.addWait(recv, reduction);
            }
        }
        graph.submit();
    }

    {
        params.m_currentOp = eHCLAllGather;
        HcclGraph graph(engine, &params);
        graph.startStrongOrder = true;
        graph.createPrim<HcclPrimAllGather>(params.m_recvBufferAddr + myBox * scaleupGroupSize * rankAddrOffset,
                                            params.m_recvBufferAddr + myBox * scaleupGroupSize * rankAddrOffset,
                                            countPerRank);

        for (uint32_t i = 1; i < boxesCount; i++)
        {
            int sendRank = (myRank + i * scaleupGroupSize) % commSize;
            int recvRank = (myRank - i * scaleupGroupSize + commSize) % commSize;

            uint64_t recvAddr    = params.m_recvBufferAddr + recvRank * rankAddrOffset;
            uint16_t recvBox     = params.m_dynamicComm.getRankToScaleupGroupMap()[recvRank];
            uint64_t boxOffset   = recvBox * scaleupGroupSize * rankAddrOffset;
            uint64_t agInOutAddr = params.m_recvBufferAddr + boxOffset;

            auto recv = graph.createPrim<HcclPrimRecv>(recvRank, recvAddr, countPerRank);
            auto ag   = graph.createPrim<HcclPrimAllGather>(agInOutAddr, agInOutAddr, countPerRank);
            graph.addWait(recv, ag);

            graph.createPrim<HcclPrimSend>(sendRank, params.m_recvBufferAddr + myRank * rankAddrOffset, countPerRank);
        }
        return graph.submit();
    }
}