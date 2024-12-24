#include "platform/gaudi3/commands/hcl_commands.h"
#include "platform/gen2_arch_common/hcl_packets_utils.h"

#include <type_traits>
#include "hccl_helpers.h"
#include "hcl_utils.h"                                // for VERIFY
#include "hcl_log_manager.h"                          // for LOG_*
#include "platform/gaudi3/hcl_packets.h"              // for serializeAllocBa...
#include "platform/gaudi3/send_recv_aggregator.h"     // for SendRecvAggregatorGaudi3
#include "platform/gaudi3/nic_passthrough_handler.h"  // for pRecordWithMetadataGaudi3
#include "platform/gaudi3/g3_sched_pkts.h"            // for g3fw

HclCommandsGaudi3::HclCommandsGaudi3() : HclCommandsGen2Arch() {}

void HclCommandsGaudi3::serializeDmaCommand(hcl::ScalStreamBase& scalStream, DmaCmdParams& cmd)
{
    uint64_t sendDataSize    = cmd.m_chunkCount * dataTypeSizeInBytes(cmd.m_dataType);
    bool     is16BitMemcpy   = isDataTypeTwoBytes(cmd.m_dataType);
    bool     useReductionInd = (cmd.m_isGDRMemcpy && !cmd.m_isFirstWrite);

    uint32_t tempDmaType;
    if (cmd.m_useSibo)
    {
        tempDmaType = g3fw::NIC_EDMA_CMD_SIBO_OPS_V3;
    }
    else
    {
        tempDmaType = g3fw::NIC_EDMA_CMD_LIN_OPS_V3;
    }

    SchedArcCommandsGaudi3::serializeDmaCommand(scalStream,
                                                cmd.m_schedIdx,
                                                tempDmaType,
                                                cmd.m_soAddressLSB,
                                                sendDataSize,
                                                cmd.m_recvBaseAddress,
                                                cmd.m_sendBaseAddress,
                                                cmd.m_reduceOp,
                                                cmd.m_streamCtxtId,
                                                cmd.m_dataType,
                                                cmd.m_poolId,
                                                cmd.m_isForScaleout,
                                                cmd.m_useCasting,
                                                cmd.m_numberOfRanks,
                                                cmd.m_numberOfSubBuffers,
                                                cmd.m_indexOfSubBuffer,
                                                is16BitMemcpy,
                                                cmd.m_soAddressLSB2,
                                                cmd.m_isBFloat,
                                                useReductionInd,
                                                cmd.m_isFirstWrite);
}

void HclCommandsGaudi3::serializeMemsetCommand(hcl::ScalStreamBase& scalStream,
                                               unsigned             schedIdx,
                                               uint64_t             addr,
                                               uint64_t             sizeInBytes,
                                               uint32_t             soAddressLSB,
                                               uint8_t              streamCtxtID,
                                               hcclDataType_t       dataType,
                                               hcclRedOp_t          reduceOp,
                                               bool                 useSibo,
                                               uint32_t             poolId,
                                               bool                 isForScaleout,
                                               uint32_t             numberOfRanks,
                                               uint32_t             numberOfSubBuffers,
                                               unsigned             indexOfSubBuffer,
                                               uint32_t             memsetValue)
{
    SchedArcCommandsGaudi3::serializeDmaCommand(scalStream,
                                                schedIdx,
                                                g3fw::NIC_EDMA_CMD_LIN_MEMSET_V3_2,
                                                soAddressLSB,
                                                sizeInBytes,
                                                addr,
                                                addr,
                                                reduceOp,
                                                streamCtxtID,
                                                dataType);
}

void HclCommandsGaudi3::serializeUpdateNicOffsets(hcl::ScalStreamBase&                     scalStream,
                                                  bool                                     isSend,
                                                  bool                                     isScaleUp,
                                                  uint32_t                                 qpn,
                                                  std::array<uint16_t, MAX_NICS_GEN2ARCH>& remoteIndices)
{
    SchedArcCommandsGaudi3::serializeUpdateNicOffsets(scalStream, isSend, isScaleUp, qpn, remoteIndices);
}

void HclCommandsGaudi3::serializeUpdateLastRank(hcl::ScalStreamBase& scalStream,
                                                bool                 isSend,
                                                bool                 isScaleUp,
                                                uint32_t             qpn,
                                                uint32_t             ports_mask)
{
    SchedArcCommandsGaudi3::serializeUpdateLastRank(scalStream, isSend, isScaleUp, qpn, ports_mask);
}

void HclCommandsGaudi3::serializeScaleUpCollectiveOp(hcl::ScalStreamBase&   scalStream,
                                                     ScaleUpCollectiveOpG3& scaleupCollectiveOp,
                                                     const unsigned         maxNumScaleUpNicsPerConnection)
{
    hcclRedOp_t effectiveReductionOp = scaleupCollectiveOp.m_isReduction ? hcclOpNone : scaleupCollectiveOp.m_reduceOp;
    SchedArcCommandsGaudi3::serializeCollectiveCommand(scalStream,
                                                       scaleupCollectiveOp.m_isSend,
                                                       true,
                                                       scaleupCollectiveOp.m_qpn,
                                                       scaleupCollectiveOp.m_disregardRank,
                                                       scaleupCollectiveOp.m_baseAddress,
                                                       scaleupCollectiveOp.m_cellCount,
                                                       scaleupCollectiveOp.m_hasBufferSize,
                                                       scaleupCollectiveOp.m_count,
                                                       scaleupCollectiveOp.m_dcore,
                                                       scaleupCollectiveOp.m_ssm,
                                                       scaleupCollectiveOp.m_sobId,
                                                       scaleupCollectiveOp.m_ports_mask,
                                                       scaleupCollectiveOp.m_collectiveOp,
                                                       effectiveReductionOp,
                                                       scaleupCollectiveOp.m_dataType,
                                                       scaleupCollectiveOp.m_ScaleupGroupSize,
                                                       maxNumScaleUpNicsPerConnection,
                                                       scaleupCollectiveOp.m_strideCount);
}

void HclCommandsGaudi3::serializeScaleOutCollectiveOp(hcl::ScalStreamBase&    scalStream,
                                                      ScaleOutCollectiveOpG3& scaleoutCollectiveOp)
{
    // When All2All collective operation is being sliced,
    // We should serialize the command several times, in order
    // to be able to control the send offset (changes per chunk and iteration)
    // and the recv offset (changes per stride and iteration)
    uint64_t all2allOffset = 0;
    uint64_t chunk         = scaleoutCollectiveOp.m_cellCount;

    if (scaleoutCollectiveOp.m_collectiveOp == eHCLAll2All)
    {
        if (scaleoutCollectiveOp.m_isSend)
        {
            all2allOffset = chunk * hccl_data_type_elem_size(scaleoutCollectiveOp.m_dataType);
        }
        else
        {
            all2allOffset =
                scaleoutCollectiveOp.m_strideCount * hccl_data_type_elem_size(scaleoutCollectiveOp.m_dataType);
        }
        LOG_HCL_DEBUG(HCL,
                      "Scaleout all2allOffset={} send={} stride={} data_typ={} chunk={}",
                      all2allOffset,
                      scaleoutCollectiveOp.m_isSend,
                      scaleoutCollectiveOp.m_strideCount,
                      scaleoutCollectiveOp.m_dataType,
                      chunk);
    }

    hcclRedOp_t effectiveReductionOp =
        (!scaleoutCollectiveOp.m_doReduction) ? hcclOpNone : scaleoutCollectiveOp.m_reduceOp;
    SchedArcCommandsGaudi3::serializeCollectiveCommand(scalStream,
                                                       scaleoutCollectiveOp.m_isSend,
                                                       false,
                                                       scaleoutCollectiveOp.m_qpn,
                                                       scaleoutCollectiveOp.m_disregardRank,
                                                       scaleoutCollectiveOp.m_baseAddress +
                                                           (all2allOffset * scaleoutCollectiveOp.m_remoteRankIteration),
                                                       chunk,
                                                       scaleoutCollectiveOp.m_hasBufferSize,
                                                       scaleoutCollectiveOp.m_count,
                                                       scaleoutCollectiveOp.m_dcore,
                                                       scaleoutCollectiveOp.m_ssm,
                                                       scaleoutCollectiveOp.m_sobId,
                                                       scaleoutCollectiveOp.m_ports_mask,
                                                       scaleoutCollectiveOp.m_collectiveOp,
                                                       effectiveReductionOp,
                                                       scaleoutCollectiveOp.m_dataType,
                                                       scaleoutCollectiveOp.m_ScaleupGroupSize,
                                                       scaleoutCollectiveOp.m_lagSize);
}

void HclCommandsGaudi3::serializeScaleUpSendRecv(hcl::ScalStreamBase&              scalStream,
                                                 const int                         selfModuleId,
                                                 const bool                        isSend,
                                                 const uint8_t                     dcore,
                                                 const uint8_t                     ssm,
                                                 const uint16_t                    sobId,
                                                 const uint32_t                    qpn,
                                                 const SendRecvArray&              sendRecvArray,
                                                 const RemoteDevicePortMasksArray& remoteDevicesPortMasks,
                                                 const HCL_Comm                    comm,
                                                 SendRecvAggregatorGaudi3&         sendRecvAggr,
                                                 const unsigned                    maxNumScaleUpNicsPerConnection)
{
    LOG_HCL_TRACE(HCL, "selfModuleId={}, isSend={}, qpn={}, ", selfModuleId, isSend, qpn);
    VERIFY(selfModuleId >= 0, "received invalid device {}", selfModuleId);

    if (GCFG_HCL_ENABLE_G3_SR_AGG.value())
    {
        sendRecvAggr.addSendRecvArray(sendRecvArray);
        // This is the last send/recv command - we need to flush either way.
        LOG_HCL_TRACE(HCL, "Before flushAggregator selfModuleId={}, isSend={}", selfModuleId, isSend);
        sendRecvAggr.flush(scalStream, comm, dcore, ssm, sobId, qpn);
        return;
    }

    uint32_t moduleId = 0;
    for (const SendRecvEntry& entry : sendRecvArray)
    {
        if (entry.isValid)
        {
            VERIFY((unsigned)selfModuleId != moduleId,
                   "Cannot scaleup to self, isSend={}, rank {}, selfModuleId={}, moduleId={}",
                   isSend,
                   entry.remoteRank,
                   selfModuleId,
                   moduleId);
            LOG_HCL_TRACE(HCL,
                          "{} rank {}, moduleId={} ",
                          isSend ? "sending to" : "receiving from",
                          entry.remoteRank,
                          moduleId);
            const uint64_t       baseAddress = entry.address;
            const uint64_t       count       = entry.count;
            const hcclDataType_t dataType    = entry.dataType;
            const uint32_t       ports_mask  = remoteDevicesPortMasks[moduleId];

            SchedArcCommandsGaudi3::serializeScaleupNonCollectiveCommand(scalStream,
                                                                         isSend,
                                                                         qpn,
                                                                         baseAddress,
                                                                         count,
                                                                         dcore,
                                                                         ssm,
                                                                         sobId,
                                                                         ports_mask,
                                                                         dataType,
                                                                         maxNumScaleUpNicsPerConnection);
        }
        moduleId++;
    }
}

void HclCommandsGaudi3::serializeScaleUpSendRecvDevice(hcl::ScalStreamBase& scalStream,
                                                       const uint16_t       deviceId,
                                                       const bool           isSend,
                                                       const uint32_t       qpn,
                                                       const uint64_t       buff,
                                                       const uint64_t       count,
                                                       const uint8_t        dcore,
                                                       const uint8_t        ssm,
                                                       const uint16_t       sobId,
                                                       const uint32_t       ports_mask,
                                                       const hcclDataType_t dataType,
                                                       const unsigned       maxNumScaleUpNicsPerConnection)
{
    LOG_HCL_TRACE(HCL, "deviceId={}, isSend={}, qpn={}", deviceId, isSend, qpn);
    VERIFY(deviceId >= 0, "received invalid device {}", deviceId);

    SchedArcCommandsGaudi3::serializeScaleupNonCollectiveCommand(scalStream,
                                                                 isSend,
                                                                 qpn,
                                                                 buff,
                                                                 count,
                                                                 dcore,
                                                                 ssm,
                                                                 sobId,
                                                                 ports_mask,
                                                                 dataType,
                                                                 maxNumScaleUpNicsPerConnection);
}

void HclCommandsGaudi3::serializeScaleUpSendRecvDeviceCmd(const bool             isSend,
                                                          const uint32_t         qpn,
                                                          const uint64_t         buff,
                                                          const uint64_t         count,
                                                          const uint8_t          dcore,
                                                          const uint8_t          ssm,
                                                          const uint16_t         sobId,
                                                          const uint32_t         ports_mask,
                                                          const hcclDataType_t   dataType,
                                                          const unsigned         maxNumScaleUpNicsPerConnection,
                                                          std::vector<uint32_t>& dwordsBuffer /* output */)
{
    LOG_HCL_TRACE(
        HCL,
        "isSend={}, qpn={}, buff=0x{:x}, count={}, dcore={}, ssm={}, sobId={}, ports_mask=0x{:x}, dataType={}",
        isSend,
        qpn,
        buff,
        count,
        dcore,
        ssm,
        sobId,
        ports_mask,
        dataType);

    gaudi3::Nic::direct_coll_desc_send_receive desc;
    constexpr size_t                           size = sizeof(desc);
    memset(&desc, 0, size);

    SchedArcCommandsGaudi3::serializeSendRecvDesc(isSend,
                                                  true /* isSend*/,
                                                  qpn,
                                                  true /* disregardRank */,
                                                  buff,
                                                  count,
                                                  false /* hasBufferSize */,
                                                  count,
                                                  dcore,
                                                  ssm,
                                                  sobId,
                                                  ports_mask,
                                                  eHCLNoCollective,
                                                  hcclOpNone,
                                                  dataType,
                                                  0 /* ScaleupGroupSize not used */,
                                                  maxNumScaleUpNicsPerConnection, /* lagSize - check with enginefw  */
                                                  0 /* strideCount not used */,
                                                  desc);
    constexpr size_t numDwords = sizeof(desc.raw) / sizeof(uint32_t);
    for (size_t dword = 0; dword < numDwords; dword++)
    {
        dwordsBuffer.push_back(desc.raw[dword]);
    }
    LOG_HCL_TRACE(HCL, "dwordsBuffer.size={}", dwordsBuffer.size());
}

void HclCommandsGaudi3::serializeNicPassthroughCommand(hcl::ScalStreamBase&             scalStream,
                                                       const bool                       isSend,
                                                       const uint32_t                   credits,
                                                       const pRecordWithMetadataGaudi3& record)
{
    SchedArcCommandsGaudi3::serializeNicPassthroughCommand(scalStream, isSend, credits, record);
}

void HclCommandsGaudi3::serializeNicNopCommand(hcl::ScalStreamBase& scalStream,
                                               const bool           isSend,
                                               const uint16_t       dupMask,
                                               const uint32_t       credits,
                                               const uint32_t       consumeDwords)
{
    SchedArcCommandsGaudi3::serializeNicNopCommand(scalStream, isSend, dupMask, credits, consumeDwords);
}

void HclCommandsGaudi3::serializeAllocBarrierCommand(hcl::ScalStreamBase& scalStream,
                                                     unsigned             schedIdx,
                                                     uint32_t             completionGroupIndex,
                                                     uint32_t             requiredSobs,
                                                     llvm_vecsmall::SmallVector<uint32_t, MAX_STREAM_TO_INC>* fences,
                                                     const LBWBurstData_t* destBurstData)
{
    SchedArcCommandsGaudi3::serializeAllocBarrierCommand(scalStream,
                                                         schedIdx,
                                                         completionGroupIndex,
                                                         requiredSobs,
                                                         fences,
                                                         destBurstData);
};

void HclCommandsGaudi3::serializeLbwWriteCommand(hcl::ScalStreamBase& scalStream,
                                                 unsigned             schedIdx,
                                                 uint32_t             destination,
                                                 uint32_t             data)
{
    SchedArcCommandsGaudi3::serializeLbwWriteCommand(scalStream, schedIdx, destination, data);
};

void HclCommandsGaudi3::serializeLbwWriteWithFenceDecCommand(hcl::ScalStreamBase& scalStream,
                                                             unsigned             schedIdx,
                                                             uint32_t             destination,
                                                             uint32_t             data,
                                                             uint32_t             fenceIndex,
                                                             uint32_t             fenceTarget)
{
    SchedArcCommandsGaudi3::serializeLbwWriteWithFenceDecCommand(scalStream,
                                                                 schedIdx,
                                                                 destination,
                                                                 data,
                                                                 fenceIndex,
                                                                 fenceTarget);
};

void HclCommandsGaudi3::serializeLbwBurstWriteCommand(hcl::ScalStreamBase&  scalStream,
                                                      unsigned              schedIdx,
                                                      const LBWBurstData_t& destData)
{
    SchedArcCommandsGaudi3::serializeLbwBurstWriteCommand(scalStream, schedIdx, destData);
};

void HclCommandsGaudi3::serializeFenceDecCommand(hcl::ScalStreamBase& scalStream,
                                                 unsigned             schedIdx,
                                                 uint32_t             fenceIndex,
                                                 uint32_t             target)
{
    SchedArcCommandsGaudi3::serializeFenceDecCommand(scalStream, schedIdx, fenceIndex, target);
};

void HclCommandsGaudi3::serializeFenceIncCommand(hcl::ScalStreamBase& scalStream,
                                                 unsigned             schedIdx,
                                                 uint32_t             fenceIndex)
{
    SchedArcCommandsGaudi3::serializeFenceIncCommand(scalStream, schedIdx, fenceIndex);
};

void HclCommandsGaudi3::serializeNopCommand(hcl::ScalStreamBase& scalStream, unsigned schedIdx, uint32_t padding)
{
    SchedArcCommandsGaudi3::serializeNopCommand(scalStream, schedIdx, padding);
}

void HclCommandsGaudi3::serializeGlobalDmaCommand(hcl::ScalStreamBase&                  scalStream,
                                                  uint32_t                              soAddressLSB,
                                                  const std::vector<sibAddressAndSize>& sibAddressesAndSizes,
                                                  uint32_t                              fwStrideSize,
                                                  uint64_t                              fwBaseAddress)
{
    SchedArcCommandsGaudi3::serializeGlobalDmaCommand(
        scalStream,
        soAddressLSB,
        sibAddressesAndSizes,
        fwStrideSize,
        fwBaseAddress,
        ScalNetworkGarbageCollectorAndReductionGroups::SCAL_EDMA_NETWORK_GC_REDUCTION_GROUP0);
}

void HclCommandsGaudi3::serializePdmaCommand(hcl::ScalStreamBase& scalStream,
                                             unsigned             schedIdx,
                                             bool                 isDownload,
                                             uint64_t             hostAddress,
                                             uint64_t             deviceAddress,
                                             uint32_t             size,
                                             bool                 isReduction,
                                             hcclRedOp_t          reduceOp,
                                             bool                 isCastUp,
                                             uint8_t              apiId,
                                             unsigned             streamIndex,
                                             hcclDataType_t       dataType,
                                             uint32_t             sobAddr,
                                             bool                 isFirstBufferUse)
{
    SchedArcCommandsGaudi3::serializePdmaCommand(scalStream,
                                                 schedIdx,
                                                 isDownload,
                                                 hostAddress,
                                                 deviceAddress,
                                                 size,
                                                 isReduction && !isFirstBufferUse,
                                                 reduceOp,
                                                 isCastUp,
                                                 apiId,
                                                 streamIndex,
                                                 getPdmaStreamCtxtId(isDownload, streamIndex),
                                                 dataType,
                                                 sobAddr);
}

void HclCommandsGaudi3::serializeSetTraceMarker(hcl::ScalStreamBase& scalStream, unsigned schedIdx, uint32_t val)
{
    static bool initializedTraceMarkerValues = false;

    if (!initializedTraceMarkerValues)
    {
        SchedArcCommandsGaudi3::serializeLbwWriteCommand(
            scalStream,
            schedIdx,
            GAUDI3_SCHED_INSTANT_STM_ADDR(1 /*die*/, g3fw::CPU_ID_SCHED_ARC3, SCHED_INSTANT_EVENT_VALUE_SCHED_TYPE),
            SCHED_STM_STREAM_PAYLOAD(0, g3fw::SCHED_TYPE_GARBAGE_REDUCTION));

        initializedTraceMarkerValues = true;
    }

    SchedArcCommandsGaudi3::serializeLbwWriteCommand(
        scalStream,
        schedIdx,
        GAUDI3_SCHED_INSTANT_STM_ADDR(1 /*die*/, g3fw::CPU_ID_SCHED_ARC3 /*cpu_id*/, SCHED_INSTANT_EVENT_TYPE_ID),
        val << 16 | g3fw::SCHED_INST_EVENT_COLLECT_TIMESTAMP);
}
