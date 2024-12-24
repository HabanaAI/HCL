#pragma once

#include "infra/scal/gen2_arch_common/scal_stream.h"
#include "platform/gen2_arch_common/types.h"
#include "hcl_api_types.h"

class HclDynamicCommunicator;
struct DmaCmdParams
{
    DmaCmdParams() {}

    explicit DmaCmdParams(unsigned         schedIdx,
                          uint32_t         soAddressLSB,
                          uint32_t         soAddressLSB2,
                          HCL_CollectiveOp collectiveOp,
                          hcclRedOp_t      reduceOp,
                          uint8_t          streamCtxtID,
                          uint64_t         chunkCount,
                          uint64_t         rankTotalChunk,
                          uint64_t         recvBaseAddress,
                          uint64_t         sendBaseAddress,
                          hcclDataType_t   dataType,
                          bool             reductionSignalToCg,
                          bool             isReduction,
                          bool             useSibo,
                          uint32_t         numberOfRanks,
                          uint32_t         numberOfSubBuffers,
                          uint32_t         indexOfSubBuffer,
                          bool             isForScaleout,
                          bool             useCasting,
                          bool             isGDRMemcpy,
                          uint32_t         poolId,
                          bool             isBFloat,
                          bool             isFirstWrite)
    : m_schedIdx(schedIdx),
      m_soAddressLSB(soAddressLSB),
      m_soAddressLSB2(soAddressLSB2),
      m_collectiveOp(collectiveOp),
      m_reduceOp(reduceOp),
      m_streamCtxtId(streamCtxtID),
      m_chunkCount(chunkCount),
      m_rankTotalChunk(rankTotalChunk),
      m_recvBaseAddress(recvBaseAddress),
      m_sendBaseAddress(sendBaseAddress),
      m_dataType(dataType),
      m_reductionSignalToCg(reductionSignalToCg),
      m_isReduction(isReduction),
      m_useSibo(useSibo),
      m_numberOfRanks(numberOfRanks),
      m_numberOfSubBuffers(numberOfSubBuffers),
      m_indexOfSubBuffer(indexOfSubBuffer),
      m_isForScaleout(isForScaleout),
      m_useCasting(useCasting),
      m_isGDRMemcpy(isGDRMemcpy),
      m_poolId(poolId),
      m_isBFloat(isBFloat),
      m_isFirstWrite(isFirstWrite)
    {
    }

    unsigned         m_schedIdx;
    uint32_t         m_dmaType;
    uint32_t         m_soAddressLSB;
    uint32_t         m_soAddressLSB2;
    HCL_CollectiveOp m_collectiveOp;
    hcclRedOp_t      m_reduceOp;
    uint8_t          m_streamCtxtId;
    uint64_t         m_chunkCount;
    uint64_t         m_rankTotalChunk;
    uint64_t         m_recvBaseAddress;
    uint64_t         m_sendBaseAddress;
    hcclDataType_t   m_dataType;
    bool             m_reductionSignalToCg;
    bool             m_isReduction;
    bool             m_useSibo;
    uint32_t         m_numberOfRanks;
    uint32_t         m_numberOfSubBuffers;
    uint32_t         m_indexOfSubBuffer;
    bool             m_isForScaleout;
    bool             m_useCasting;
    bool             m_isGDRMemcpy;
    uint32_t         m_poolId;
    bool             m_isBFloat;
    bool             m_isFirstWrite;
};

struct ScaleUpCollectiveOp
{
    explicit ScaleUpCollectiveOp(box_devices_t&          deviceToRemoteIndex,
                                 uint32_t                selfModuleId,
                                 HclDynamicCommunicator& dynamicComm,
                                 HCL_CollectiveOp        collectiveOp,
                                 hcclRedOp_t             reduceOp,
                                 unsigned                collectiveContextIndex,  // 0..7
                                 uint32_t                soAddress,
                                 bool                    isSend,
                                 bool                    isComplexCollective,
                                 bool                    isReductionInIMB,
                                 bool                    isHierarchical,
                                 uint64_t                baseAddress,
                                 uint64_t                count,
                                 bool                    hasBufferSize,
                                 hcclDataType_t          dataType,
                                 uint64_t                cellCount,
                                 uint64_t                strideCount,
                                 bool                    notifyRndvAck,
                                 bool                    waitForRndvAcks,
                                 bool                    isReduction,
                                 uint32_t                accuIndex,
                                 uint32_t                subBuffIndex,
                                 HCL_CollectiveOp        complexCollective,
                                 bool                    isRoot)
    : m_deviceToRemoteIndex(deviceToRemoteIndex),
      m_selfModuleId(selfModuleId),
      m_dynamicComm(dynamicComm),
      m_collectiveOp(collectiveOp),
      m_reduceOp(reduceOp),
      m_collectiveContextIndex(collectiveContextIndex),  // 0..7
      m_soAddress(soAddress),
      m_isSend(isSend),
      m_isComplexCollective(isComplexCollective),
      m_isReductionInIMB(isReductionInIMB),
      m_isHierarchical(isHierarchical),
      m_baseAddress(baseAddress),
      m_count(count),
      m_hasBufferSize(hasBufferSize),
      m_dataType(dataType),
      m_cellCount(cellCount),
      m_strideCount(strideCount),
      m_notifyRndvAck(notifyRndvAck),
      m_waitForRndvAcks(waitForRndvAcks),
      m_isReduction(isReduction),
      m_accuIndex(accuIndex),
      m_subBuffIndex(subBuffIndex),
      m_complexCollective(complexCollective),
      m_isRoot(isRoot)
    {
    }

    explicit ScaleUpCollectiveOp(HclDynamicCommunicator& dynamicComm, box_devices_t& deviceToRemoteIndex)
    : m_deviceToRemoteIndex(deviceToRemoteIndex), m_dynamicComm(dynamicComm)
    {
    }

    box_devices_t&          m_deviceToRemoteIndex;
    uint32_t                m_selfModuleId;
    HclDynamicCommunicator& m_dynamicComm;
    HCL_CollectiveOp        m_collectiveOp;
    hcclRedOp_t             m_reduceOp;
    unsigned                m_collectiveContextIndex;  // 0..7
    uint32_t                m_soAddress;
    bool                    m_isSend;
    bool                    m_isComplexCollective;
    bool                    m_isReductionInIMB;
    bool                    m_isHierarchical;
    uint64_t                m_baseAddress;
    uint64_t                m_count;
    bool                    m_hasBufferSize;
    hcclDataType_t          m_dataType;
    uint64_t                m_cellCount;
    uint64_t                m_strideCount;
    bool                    m_notifyRndvAck;
    bool                    m_waitForRndvAcks;
    bool                    m_isReduction       = false;
    uint32_t                m_accuIndex         = 0;
    uint32_t                m_subBuffIndex      = 0;
    HCL_CollectiveOp        m_complexCollective = eHCLNoCollective;
    bool                    m_isRoot            = false;
};

struct ScaleOutCollectiveOp
{
    explicit ScaleOutCollectiveOp(uint16_t         myScaleupGroup,
                                  const int        remoteRankToRsi,
                                  HCL_Comm         comm,
                                  HCL_CollectiveOp collectiveOp,
                                  hcclRedOp_t      reduceOp,
                                  unsigned         collectiveContextIndex,  // 0..7
                                  uint32_t         soAddress,
                                  bool             isSend,
                                  bool             isReductionInIMB,
                                  uint64_t         baseAddress,
                                  uint64_t         count,
                                  bool             hasBufferSize,
                                  hcclDataType_t   dataType,
                                  uint64_t         cellCount,
                                  uint64_t         strideCount,
                                  const HCL_Rank   remoteRank,  // remote scaleout rank
                                  uint32_t         remoteRankIteration,
                                  bool             notifyRndvAck,
                                  bool             waitForRndvAcks,
                                  bool             doReduction,
                                  uint8_t          qpSet)
    : m_myScaleupGroup(myScaleupGroup),
      m_remoteRankToRsi(remoteRankToRsi),
      m_comm(comm),
      m_collectiveOp(collectiveOp),
      m_reduceOp(reduceOp),
      m_collectiveContextIndex(collectiveContextIndex),  // 0..7
      m_soAddress(soAddress),
      m_isSend(isSend),
      m_isReductionInIMB(isReductionInIMB),
      m_baseAddress(baseAddress),
      m_count(count),
      m_hasBufferSize(hasBufferSize),
      m_dataType(dataType),
      m_cellCount(cellCount),
      m_strideCount(strideCount),
      m_remoteRank(remoteRank),  // remote scaleout rank
      m_remoteRankIteration(remoteRankIteration),
      m_notifyRndvAck(notifyRndvAck),
      m_waitForRndvAcks(waitForRndvAcks),
      m_doReduction(doReduction),
      m_qpSet(qpSet)
    {
    }

    ScaleOutCollectiveOp() {}

    uint16_t         m_myScaleupGroup;
    int              m_remoteRankToRsi;
    HCL_Comm         m_comm;
    HCL_CollectiveOp m_collectiveOp;
    hcclRedOp_t      m_reduceOp;
    unsigned         m_collectiveContextIndex;  // 0..7
    uint32_t         m_soAddress;
    bool             m_isSend;
    bool             m_isReductionInIMB;
    uint64_t         m_baseAddress;
    uint64_t         m_count;
    bool             m_hasBufferSize;
    hcclDataType_t   m_dataType;
    uint64_t         m_cellCount;
    uint64_t         m_strideCount;
    HCL_Rank         m_remoteRank;  // remote scaleout rank
    uint32_t         m_remoteRankIteration;
    bool             m_notifyRndvAck;
    bool             m_waitForRndvAcks;
    bool             m_doReduction;
    uint8_t          m_qpSet;
};
