#pragma once

#include "infra/scal/gen2_arch_common/scal_stream.h"
#include "platform/gen2_arch_common/intermediate_buffer_container.h"
#include "hcl_utils.h"
#include "platform/gen2_arch_common/commands/hcl_commands_types.h"
#include "internal/hcl_profiler_api.h"
#include "platform/gen2_arch_common/hcl_device_controller.h"

namespace hcl
{
class ScalStreamBase;

constexpr uint8_t DEFAULT_STREAM_IDX = 0;

}  // namespace hcl

class HclDeviceGen2Arch;
struct SendRecvEntry;

class HclCommandsGen2Arch
{
public:
    HclCommandsGen2Arch()                                      = default;
    HclCommandsGen2Arch(HclCommandsGen2Arch&&)                 = delete;
    HclCommandsGen2Arch(const HclCommandsGen2Arch&)            = delete;
    HclCommandsGen2Arch& operator=(HclCommandsGen2Arch&&)      = delete;
    HclCommandsGen2Arch& operator=(const HclCommandsGen2Arch&) = delete;
    virtual ~HclCommandsGen2Arch()                             = default;

    virtual void serializeDmaCommand(hcl::ScalStreamBase& scalStream, DmaCmdParams& cmd) = 0;

    virtual void serializeMemsetCommand(hcl::ScalStreamBase& scalStream,
                                        unsigned             schedIdx,
                                        uint64_t             addr,
                                        uint64_t             sizeInBytes,
                                        uint32_t             soAddressLSB,
                                        uint8_t              streamCtxtID,
                                        hcclDataType_t       dataType,
                                        hcclRedOp_t          reduceOp           = hcclOpNone,
                                        bool                 useSibo            = false,
                                        uint32_t             poolId             = 0,
                                        bool                 isForScaleout      = false,
                                        uint32_t             numberOfRanks      = 0,
                                        uint32_t             numberOfSubBuffers = 0,
                                        uint32_t             indexOfSubBuffer   = 0,
                                        uint32_t             memsetValue        = 0) = 0;

    virtual void
    serializeAllocBarrierCommand(hcl::ScalStreamBase&                                     scalStream,
                                 unsigned                                                 schedIdx,
                                 uint32_t                                                 completionGroupIndex,
                                 uint32_t                                                 requiredSobs,
                                 llvm_vecsmall::SmallVector<uint32_t, MAX_STREAM_TO_INC>* fences = nullptr) = 0;

    virtual void serializeLbwWriteCommand(hcl::ScalStreamBase& scalStream,
                                          unsigned             schedIdx,
                                          uint32_t             destination,
                                          uint32_t             data,
                                          bool                 blockUntilCompletion = false) = 0;

    virtual void serializeLbwWriteWithFenceDecCommand(hcl::ScalStreamBase& scalStream,
                                                      unsigned             schedIdx,
                                                      uint32_t             destination,
                                                      uint32_t             data,
                                                      uint32_t             fenceIndex,
                                                      uint32_t             fenceTarget          = 1,
                                                      bool                 blockUntilCompletion = false) = 0;

    virtual void serializeLbwBurstWriteCommand(hcl::ScalStreamBase&      scalStream,
                                               unsigned                  schedIdx,
                                               const LBWBurstDestData_t& destData,
                                               bool                      blockUntilCompletion = false) = 0;

    virtual void serializeFenceDecCommand(hcl::ScalStreamBase& scalStream,
                                          unsigned             schedIdx,
                                          uint32_t             fenceIndex,
                                          uint32_t             target = 1) = 0;

    virtual void serializeSetTraceMarker(hcl::ScalStreamBase& scalStream, unsigned schedIdx, uint32_t val) = 0;

    /**
     * @brief Update FW with the SIMB base address and stride size, to allow batch reduction via EDMA.
     *
     * @param scalStream
     * @param soAddressLSB
     * @param strideSize the diff between buffers (in bytes)
     * @param baseAddress the base address of the required SIMB pool
     * @param fwBaseAddress the base address of the required FW SIMB pool
     * @param engineType DMA engine type
     */
    virtual void serializeGlobalDmaCommand(hcl::ScalStreamBase&                  scalStream,
                                           uint32_t                              soAddressLSB,
                                           const std::vector<sibAddressAndSize>& sibAddressesAndSizes,
                                           uint32_t                              fwStrideSize,
                                           uint64_t                              fwBaseAddress) = 0;

    virtual void serializeFenceIncCommand(hcl::ScalStreamBase& scalStream, unsigned schedIdx, uint32_t fenceIndex) = 0;

    virtual void serializeNopCommand(hcl::ScalStreamBase& scalStream, unsigned schedIdx, uint32_t padding) = 0;

    virtual void serializePdmaCommand(hcl::ScalStreamBase& scalStream,
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
                                      uint32_t             sobAddr          = 0,
                                      bool                 isFirstBufferUse = false) = 0;

public:
    virtual bool     isCastDown(uint32_t dmaType) = 0;
    virtual bool     isCastUp(uint32_t dmaType)   = 0;
    virtual bool     isMemCpy(uint32_t dmaType)   = 0;
    virtual unsigned getDmaTypeCastUp()           = 0;
    virtual unsigned getDmaTypeCastDown()         = 0;
    virtual unsigned getDmaTypeMemCpy()           = 0;
};
