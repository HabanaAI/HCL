#pragma once

#include "infra/scal/gen2_arch_common/scal_stream.h"
#include "platform/gen2_arch_common/device_buffer_manager.h"
#include "platform/gen2_arch_common/intermediate_buffer_container.h"
#include "platform/gen2_arch_common/signals/manager.h"
#include "hcl_utils.h"
#include "platform/gen2_arch_common/commands/hcl_commands_types.h"
#include "internal/hcl_profiler_api.h"

namespace hcl
{
class ScalStreamBase;

constexpr uint8_t DEFAULT_STREAM_IDX = 0;

inline uint8_t encodeStreamContextID(uint8_t apiId, unsigned streamIndex)
{
    StreamContextEncoding streamCtxtID;

    // Ensure apiId and streamIndex are within the valid range
    streamCtxtID.api_id       = apiId & 0b11111;     // 5 bits
    streamCtxtID.stream_index = streamIndex & 0b11;  // 2 bits

    return streamCtxtID.raw;
}
}  // namespace hcl

class HclDeviceGen2Arch;
struct SendRecvEntry;

class HclCommandsGen2Arch
{
public:
    HclCommandsGen2Arch()                           = default;
    HclCommandsGen2Arch(HclCommandsGen2Arch&&)      = delete;
    HclCommandsGen2Arch(const HclCommandsGen2Arch&) = delete;
    HclCommandsGen2Arch& operator=(HclCommandsGen2Arch&&) = delete;
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
                                        hcclRedOp_t          reduceOp             = hcclOpNone,
                                        bool                 useSibo              = false,
                                        uint32_t             poolId               = 0,
                                        bool                 isForScaleout        = false,
                                        uint32_t             numberOfRanks        = 0,
                                        uint32_t             numberOfReproBuffers = 0,
                                        uint32_t             indexOfReproBuffer   = 0) = 0;

    virtual void serializeAllocBarrierCommand(hcl::ScalStreamBase& scalStream,
                                              unsigned             schedIdx,
                                              uint32_t             completionGroupIndex,
                                              uint32_t             requiredSobs) = 0;

    virtual void serializeLbwWriteCommand(hcl::ScalStreamBase& scalStream,
                                          unsigned             schedIdx,
                                          uint32_t             destination,
                                          uint32_t             data,
                                          bool                 blockUntilCompletion = false) = 0;

    virtual void serializeFenceCommand(hcl::ScalStreamBase& scalStream,
                                       unsigned             schedIdx,
                                       uint32_t             fenceIndex,
                                       uint32_t             target = 1) = 0;

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

    virtual void memsetIMBs(DeviceBufferManager&              imb,
                            hcl::IntermediateBufferContainer* imbContainer,
                            SignalsManager*                   signalsManager,
                            SliceState&                       sendSliceState,
                            SliceState&                       recvSliceState,
                            unsigned int                      sizeInBytes,
                            hcl::syncInfo                     longSo,
                            unsigned                          schedIdx,
                            hcl::ScalStream&                  garbageCollectionStream,
                            HCL_StreamId                      m_streamId,
                            e_devicePoolID                    poolId,
                            uint8_t                           streamCtxtID,
                            hcclDataType_t                    dataType) = 0;

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
