#pragma once

#include <cstddef>  // for size_t
#include <cstdint>  // for uint32_t, uint64_t
#include <array>    // for array
#include <map>
#include <vector>

#include "hcl_types.h"
#include "platform/gaudi2/types.h"
#include "g2_sched_pkts.h"  // for g2fw
#include "platform/gen2_arch_common/host_stream.h"
#include "hcl_api_types.h"
#include "platform/gen2_arch_common/device_simb_pool_manager.h"
#include "platform/gen2_arch_common/commands/hcl_commands_types.h"
#include "platform/gaudi2/nic_passthrough_handler.h"  // for pRecordWithMetadata
#include "platform/gaudi2/context_manager.h"
#include "platform/gen2_arch_common/hcl_device_controller.h"

namespace hcl
{
class ScalStreamBase;
};

namespace SchedArcCommandsGaudi2
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
                         uint32_t             memsetValue        = 0);

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
                          hcclDataType_t       dataType,
                          uint32_t             sobAddr = 0);

void serializeGlobalDmaCommand(hcl::ScalStreamBase&                                 scalStream,
                               unsigned                                             schedIdx,
                               uint32_t                                             soAddressLSB,
                               const std::vector<SimbPoolContainerParamsPerStream>& containerParamsPerStreamVec,
                               uint32_t                                             fwStrideSize,
                               uint64_t                                             fwBaseAddress,
                               uint32_t                                             engineType);

void serializeUpdateGlobalContextCommandHeader(g2fw::sched_arc_cmd_update_nic_glbl_ctxt_t& command,
                                               uint32_t                                    soAddressLSB,
                                               uint32_t                                    numDwords);

void serializeUpdateGlobalContextCommand(hcl::ScalStreamBase&                scalStream,
                                         uint32_t                            soAddressLSB,
                                         std::vector<g2fw::nic_glbl_ctxt_t>& contexts);

void serializeUpdateGlobalContextInfo(hcl::ScalStreamBase& scalStream,
                                      uint32_t             soAddressLSB,
                                      uint64_t             sib_order_base_addr,
                                      uint32_t             sibo_rank_stride);

void serializeUpdateGlobalContextScaleOutCommand(hcl::ScalStreamBase&                scalStream,
                                                 uint32_t                            soAddressLSB,
                                                 std::vector<g2fw::nic_glbl_ctxt_t>& contexts,
                                                 uint32_t                            start_nic_index);

void serializeUpdateCollectiveContextCommand(hcl::ScalStreamBase&           scalStream,
                                             bool                           isSend,
                                             unsigned                       collectiveContextIndex,
                                             unsigned                       commDescIndex,
                                             ContextManager::ContextValues& contextValues);

void serializeCollectiveSendShortCommand(hcl::ScalStreamBase& scalStream,
                                         unsigned             collectiveContextIndex,
                                         unsigned             commDescIndex,
                                         bool                 isSend,
                                         bool                 hasBufferSize,
                                         uint32_t             bufferSize,
                                         bool                 force_remote_rank_offset,
                                         uint32_t             cacheLineCount,
                                         uint32_t             cacheLineRemainder,
                                         uint8_t              elementRemainder,
                                         uint32_t             address,  // lsb
                                         bool                 notifyRndvAck   = false,
                                         bool                 waitForRndvAcks = false);

void serializeCollectiveRecvShortInOrderCommand(hcl::ScalStreamBase& scalStream,
                                                unsigned             collectiveContextIndex,
                                                unsigned             commDescIndex,
                                                uint32_t             cacheLineCount,
                                                uint32_t             currentRank,
                                                uint32_t             subBuffIndex,
                                                uint32_t             poolId,
                                                bool                 notifyRndvAck   = false,
                                                bool                 waitForRndvAcks = false);

void serializeCollectiveSendLongCommand(hcl::ScalStreamBase& scalStream,
                                        unsigned             collectiveContextIndex,
                                        unsigned             commDescIndex,
                                        bool                 isSend,
                                        bool                 hasBufferSize,
                                        uint32_t             bufferSize,
                                        bool                 force_remote_rank_offset,
                                        uint32_t             cacheLineCount,
                                        uint32_t             cacheLineRemainder,
                                        uint8_t              elementRemainder,
                                        uint64_t             address,
                                        bool                 notifyRndvAck   = false,
                                        bool                 waitForRndvAcks = false);

void serializeCollectiveSendScaleOutCommand(hcl::ScalStreamBase&           scalStream,
                                            unsigned                       collectiveContextIndex,
                                            bool                           isSend,
                                            bool                           hasBufferSize,
                                            uint32_t                       bufferSize,
                                            unsigned                       syncObjectAddressIndex,
                                            uint32_t                       cacheLineCount,
                                            uint32_t                       cacheLineRemainder,
                                            uint8_t                        elementRemainder,
                                            uint64_t                       address,
                                            ContextManager::ContextValues& contextValues,
                                            std::array<uint16_t, 4>&       qpnDesc,
                                            bool                           notifyRndvAck,
                                            bool                           waitForRndvAcks);

void serializeUserSendCommand(std::vector<uint32_t>& out,
                              unsigned               collectiveContextIndex,
                              unsigned               commDescIndex,
                              unsigned               syncObjectAddressIndex,
                              uint32_t               cacheLineCount,
                              uint32_t               cacheLineRemainder,
                              uint8_t                elementRemainder,
                              hcclDataType_t         dataType,
                              uint64_t               address,
                              bool                   isLastInGroup   = false,
                              bool                   notifyRndvAck   = false,
                              bool                   waitForRndvAcks = false);

void serializeNicNopCommand(pRecordWithMetadata& records,
                            unsigned             collectiveContextIndex,
                            uint32_t             dupMask,
                            size_t               requiredCredits,
                            unsigned             syncObjectAddressIndex,
                            bool                 incSOB = false);

size_t recordsSizeInDwords(std::vector<pRecordWithMetadata>& records);

void serializeNicPassthroughCommand(hcl::ScalStreamBase&              scalStream,
                                    std::vector<pRecordWithMetadata>& records,
                                    size_t                            credits,
                                    bool                              isSend);

}  // namespace SchedArcCommandsGaudi2
