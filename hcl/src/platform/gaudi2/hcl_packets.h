#pragma once

#include <cstddef>  // for size_t
#include <cstdint>  // for uint32_t, uint64_t
#include <array>    // for array
#include <map>
#include <vector>

#include "hcl_types.h"
#include "platform/gaudi2/types.h"
#include "sched_pkts.h"
#include "platform/gen2_arch_common/host_stream.h"
#include "hcl_api_types.h"
#include "platform/gen2_arch_common/device_buffer_manager.h"
#include "platform/gaudi2/nic_passthrough_handler.h"  // for pRecordWithMetadata
#include "platform/gaudi2/context_manager.h"

#ifdef NDEBUG
#define SET_FIELD(field, value)                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        (field) = (value);                                                                                             \
    } while (0);
#else
#define SET_FIELD(field, value)                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        (field) = (value);                                                                                             \
        VERIFY((field) == (value), "The values {},{} are not equal.", field, value);                                   \
    } while (0);
#endif

namespace hcl
{
class ScalStreamBase;
};

namespace SchedArcCommandsGaudi2
{
void serializeNopCommand(hcl::ScalStreamBase& scalStream, unsigned schedIdx, uint32_t padding);

void serializeAllocBarrierCommand(hcl::ScalStreamBase& scalStream,
                                  unsigned             schedIdx,
                                  uint32_t             completionGroupIndex,
                                  uint32_t             requiredSobs);

void serializeFenceCommand(hcl::ScalStreamBase& scalStream,
                           unsigned             schedIdx,
                           uint32_t             fenceIndex,
                           uint32_t             target = 1);

void serializeFenceIncCommand(hcl::ScalStreamBase& scalStream,
                              unsigned             schedIdx,
                              uint32_t             fenceIndex,
                              uint32_t             target = 1);

void serializeLbwWriteCommand(hcl::ScalStreamBase& scalStream,
                              unsigned             schedIdx,
                              uint32_t             destination,
                              uint32_t             data,
                              bool                 blockUntilCompletion = false);

void serializeDmaCommandV2(hcl::ScalStreamBase& scalStream,
                           unsigned             schedIdx,
                           uint32_t             dmaType,
                           uint32_t             soAddressLSB,
                           uint32_t             soAddressLSB2,
                           uint32_t             size,
                           uint64_t             destAddress,
                           uint64_t             srcAddress,
                           hcclRedOp_t          reduceOp,
                           bool                 isReduction,
                           bool                 reductionSignalToCg,
                           hcclDataType_t       dataType,
                           uint32_t             poolId               = 0,
                           bool                 isReproReduction     = false,
                           bool                 useSibo              = false,
                           uint32_t             numberOfRanks        = 0,
                           uint32_t             numberOfReproBuffers = 0,
                           uint32_t             indexOfReproBuffer   = 0,
                           bool                 is16BitMemcpy        = false,
                           bool                 isGDRMemcpy          = false);

void serializeDmaCommandV3(hcl::ScalStreamBase& scalStream,
                           unsigned             schedIdx,
                           uint32_t             dmaType,
                           uint32_t             soAddressLSB,
                           uint32_t             size,
                           uint64_t             destAddress,
                           uint64_t             srcAddress,
                           hcclRedOp_t          reduceOp,
                           uint8_t              streamCtxtID,
                           hcclDataType_t       dataType,
                           uint32_t             poolId               = 0,
                           bool                 isForScaleout        = false,
                           bool                 useCasting           = false,
                           uint32_t             numberOfRanks        = 0,
                           uint32_t             numberOfReproBuffers = 0,
                           uint32_t             indexOfReproBuffer   = 0,
                           bool                 is16BitMemcpy        = false,
                           uint32_t             secondSoAddress      = 0,
                           bool                 isBFloat             = false,
                           bool                 useReductionInd      = false);

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

uint8_t getPdmaCtxtId(bool isDownload, unsigned streamIndex);

void serializeGlobalDmaCommandV2(hcl::ScalStreamBase&                  scalStream,
                                 uint32_t                              soAddressLSB,
                                 const std::vector<sibAddressAndSize>& sibAddressesAndSizes,
                                 uint32_t                              engineType);

void serializeGlobalDmaCommandV3(hcl::ScalStreamBase&                  scalStream,
                                 uint32_t                              soAddressLSB,
                                 const std::vector<sibAddressAndSize>& sibAddressesAndSizes,
                                 uint32_t                              fwStrideSize,
                                 uint64_t                              fwBaseAddress,
                                 uint32_t                              engineType);

void serializeUpdateGlobalContextCommand(hcl::ScalStreamBase&                scalStream,
                                         uint32_t                            soAddressLSB,
                                         std::vector<g2fw::nic_glbl_ctxt_t>& contexts,
                                         uint64_t                            sib_order_base_addr = 0,
                                         uint64_t                            sib_acc_base_addr   = 0,
                                         uint32_t                            sibo_rank_stride    = 0,
                                         uint32_t                            siba_stride         = 0);

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
                                         unsigned             syncObjectAddressIndex,
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
                                                bool                 hasBufferSize,
                                                unsigned             syncObjectAddressIndex,
                                                uint32_t             cacheLineCount,
                                                uint32_t             currentRank,
                                                uint32_t             accuIndex,
                                                uint32_t             rrIndex,
                                                uint32_t             numOfRanks,
                                                uint8_t              nicsBitmap,
                                                uint32_t             poolId);

void serializeCollectiveSendLongCommand(hcl::ScalStreamBase& scalStream,
                                        unsigned             collectiveContextIndex,
                                        unsigned             commDescIndex,
                                        bool                 isSend,
                                        bool                 hasBufferSize,
                                        uint32_t             bufferSize,
                                        unsigned             syncObjectAddressIndex,
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
