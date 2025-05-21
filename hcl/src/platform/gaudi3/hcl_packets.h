#pragma once

#include <array>
#include <cstdint>  // for uint32_t

#include "hcl_api_types.h"  // for HCL_CollectiveOp
#include "platform/gen2_arch_common/types.h"
#include "hccl_types.h"  // for hcclRedOp_t
#include "platform/gen2_arch_common/device_simb_pool_manager.h"
#include "gaudi3/nic_patcher_cmds.h"                  // for direct_coll_desc_send_receive
#include "platform/gaudi3/nic_passthrough_handler.h"  // for pRecordWithMetadataGaudi3
#include "platform/gen2_arch_common/commands/hcl_commands.h"

namespace hcl
{
class ScalStreamBase;
};

namespace SchedArcCommandsGaudi3
{
void serializeNopCommand(hcl::ScalStreamBase& scalStream, unsigned schedIdx, uint32_t padding);

void serializeAllocBarrierCommand(hcl::ScalStreamBase&                                        scalStream,
                                  unsigned                                                    schedIdx,
                                  uint32_t                                                    completionGroupIndex,
                                  uint32_t                                                    requiredSobs,
                                  llvm_vecsmall::SmallVector<uint32_t, MAX_STREAM_PER_SCHED>* fences        = nullptr,
                                  const LBWBurstData_t*                                       destBurstData = nullptr);

void serializeFenceDecCommand(hcl::ScalStreamBase& scalStream,
                              unsigned             schedIdx,
                              uint32_t             fenceIndex,
                              uint32_t             target = 1);

void serializeFenceIncCommand(hcl::ScalStreamBase& scalStream,
                              unsigned             schedIdx,
                              uint32_t             fenceIndex,
                              uint32_t             target = 1);

void serializeLbwWriteCommand(hcl::ScalStreamBase& scalStream, unsigned schedIdx, uint32_t destination, uint32_t data);

void serializeLbwWriteWithFenceDecCommand(hcl::ScalStreamBase& scalStream,
                                          unsigned             schedIdx,
                                          uint32_t             destination,
                                          uint32_t             data,
                                          uint32_t             fenceIndex,
                                          uint32_t             fenceTarget = 1);

void serializeLbwBurstWriteCommand(hcl::ScalStreamBase& scalStream, unsigned schedIdx, const LBWBurstData_t& destData);

void serializeDmaCommand(hcl::ScalStreamBase& scalStream,
                         unsigned             schedIdx,
                         uint32_t             dmaType,
                         uint32_t             soAddressLSB,
                         uint32_t             size,
                         uint64_t             destAddress,
                         uint64_t             srcAddress,
                         hcclRedOp_t          reduceOp,
                         uint8_t              streamCtxtID,
                         hcclDataType_t       dataType,
                         uint32_t             poolId             = 0,
                         bool                 isForScaleout      = false,
                         bool                 useCasting         = false,
                         uint32_t             numberOfRanks      = 0,
                         uint32_t             numberOfSubBuffers = 0,
                         uint32_t             indexOfSubBuffer   = 0,
                         bool                 is16BitMemcpy      = false,
                         uint32_t             secondSoAddress    = 0,
                         bool                 isBFloat           = false,
                         bool                 useReductionInd    = false,
                         bool                 isFirstWrite       = false,
                         uint32_t             memsetValue        = 0,
                         bool                 isWideAccumulation = false);

void serializePdmaCommand(hcl::ScalStreamBase& scalStream,
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
                          uint8_t              streamCtxtID,
                          hcclDataType_t       dataType,
                          uint32_t             sobAddr = 0);

void serializeCollectiveCommand(hcl::ScalStreamBase& scalStream,
                                bool                 isSend,
                                bool                 isScaleUp,
                                uint32_t             qpn,
                                bool                 disregardRank,
                                uint64_t             buff,
                                uint64_t             cellCount,
                                bool                 hasBufferSize,
                                uint64_t             count,
                                uint8_t              dcore,
                                uint8_t              ssm,
                                uint16_t             sobId,
                                uint32_t             ports_mask,
                                HCL_CollectiveOp     collectiveOp,
                                hcclRedOp_t          reduceOp,
                                hcclDataType_t       dataType,
                                uint32_t             ScaleupGroupSize,
                                uint32_t             lagSize,
                                uint64_t             strideCount   = 1,
                                bool                 isRSContReduc = false);

void serializeSendRecvDesc(const bool                                  isSend,
                           const bool                                  isScaleUp,
                           const uint32_t                              qpn,
                           const bool                                  disregardRank,
                           const uint64_t                              buff,
                           const uint64_t                              cellCount,
                           const bool                                  hasBufferSize,
                           const uint64_t                              count,
                           const uint8_t                               dcore,
                           const uint8_t                               ssm,
                           const uint16_t                              sobId,
                           const uint32_t                              ports_mask,
                           const HCL_CollectiveOp                      collectiveOp,
                           const hcclRedOp_t                           reduceOp,
                           const hcclDataType_t                        dataType,
                           const uint32_t                              ScaleupGroupSize,
                           const uint32_t                              lagSize,
                           const uint64_t                              strideCount,
                           gaudi3::Nic::direct_coll_desc_send_receive& desc,
                           bool                                        isRSContReduc = false);

void serializeScaleupNonCollectiveCommand(hcl::ScalStreamBase& scalStream,
                                          const bool           isSend,
                                          const uint32_t       qpn,
                                          const uint64_t       buff,
                                          const uint64_t       count,
                                          const uint8_t        dcore,
                                          const uint8_t        ssm,
                                          const uint16_t       sobId,
                                          const uint32_t       ports_mask,
                                          const hcclDataType_t dataType,
                                          const unsigned       maxNumScaleUpNicsPerConnection);

void serializeNicPassthroughCommand(hcl::ScalStreamBase&             scalStream,
                                    const bool                       isSend,
                                    const uint32_t                   credits,
                                    const pRecordWithMetadataGaudi3& record);

void serializeNicNopCommand(hcl::ScalStreamBase& scalStream,
                            const bool           isSend,
                            const uint16_t       dupMask,  // dup mask of nic macros pair to send NOP command to
                            const uint32_t       credits,
                            const uint32_t       consumeDwords);

void serializeGlobalDmaCommand(hcl::ScalStreamBase&                                 scalStream,
                               unsigned                                             schedIdx,
                               uint32_t                                             soAddressLSB,
                               const std::vector<SimbPoolContainerParamsPerStream>& containerParamsPerStreamVec,
                               uint32_t                                             fwStrideSize,
                               uint64_t                                             fwBaseAddress,
                               uint32_t                                             engineType);

void serializeUpdateNicOffsets(hcl::ScalStreamBase&                     scalStream,
                               bool                                     isSend,
                               bool                                     isScaleUp,
                               uint32_t                                 qpn,
                               std::array<uint16_t, MAX_NICS_GEN2ARCH>& remoteIndices);

void serializeUpdateLastRank(hcl::ScalStreamBase& scalStream,
                             bool                 isSend,
                             bool                 isScaleUp,
                             uint32_t             qpn,
                             uint32_t             ports_mask);

}  // namespace SchedArcCommandsGaudi3
