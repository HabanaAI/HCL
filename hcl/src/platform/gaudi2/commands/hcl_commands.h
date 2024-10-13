#pragma once

#include <cstddef>  // for size_t
#include <cstdint>  // for uint32_t
#include <array>    // for array
#include <vector>   // for vector

#include "hcl_api_types.h"                                    // for HCL_Comm
#include "platform/gaudi2/types.h"                            // for pRecord...
#include "platform/gen2_arch_common/commands/hcl_commands.h"  // for HclComm...
#include "platform/gaudi2/send_recv_aggregator.h"             // for
#include "platform/gen2_arch_common/send_recv_aggregator.h"   // for SendRecvEntry
#include "hccl_types.h"                                       // for hcclRedOp_t
#include "platform/gaudi2/nic_passthrough_handler.h"          // for pRecordWithMetadata

class ContextManager;
class RequiredCollectiveContext;
namespace hcl
{
class ScalStreamBase;
}

struct DmaCmdParamsG2 : public DmaCmdParams
{
    explicit DmaCmdParamsG2(DmaCmdParams& other) : DmaCmdParams(other) {};

    uint64_t m_sendDataSize;
    bool     m_isReduction;
    uint32_t m_tempDmaType;
    bool     m_isBf16Memcpy;
};

struct ScaleUpCollectiveOpG2 : public ScaleUpCollectiveOp
{
    explicit ScaleUpCollectiveOpG2(ScaleUpCollectiveOp& other, ContextManager& contextManager)
    : ScaleUpCollectiveOp(other), m_contextManager(contextManager)
    {
    }

    explicit ScaleUpCollectiveOpG2(box_devices_t&          deviceToRemoteIndex,
                                   ContextManager&         contextManager,
                                   HclDynamicCommunicator& dynamicComm)
    : ScaleUpCollectiveOp(dynamicComm, deviceToRemoteIndex), m_contextManager(contextManager)
    {
    }

    ContextManager& m_contextManager;
    int             m_currentRank;
    HCL_Comm        m_comm;
    int             m_numOfRanks = 8;
    uint32_t        m_poolId     = 0;
};

struct ScaleOutCollectiveOpG2 : public ScaleOutCollectiveOp
{
    explicit ScaleOutCollectiveOpG2(ScaleOutCollectiveOp& other, ContextManager& contextManager)
    : ScaleOutCollectiveOp(other), m_contextManager(contextManager)
    {
    }

    explicit ScaleOutCollectiveOpG2(ContextManager& contextManager) : m_contextManager(contextManager) {}

    ContextManager& m_contextManager;
};

class HclCommandsGaudi2 : public HclCommandsGen2Arch
{
public:
    HclCommandsGaudi2();
    HclCommandsGaudi2(HclCommandsGaudi2&&)                 = delete;
    HclCommandsGaudi2(const HclCommandsGaudi2&)            = delete;
    HclCommandsGaudi2& operator=(HclCommandsGaudi2&&)      = delete;
    HclCommandsGaudi2& operator=(const HclCommandsGaudi2&) = delete;
    virtual ~HclCommandsGaudi2()                           = default;

    virtual void serializeDmaCommand(hcl::ScalStreamBase& scalStream, DmaCmdParams& cmd) override;

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
                                        uint32_t             memsetValue        = 0) override;

    virtual void serializeInitSequenceCommands(hcl::ScalStreamBase&                  recvStream,
                                               hcl::ScalStreamBase&                  recvSOStream,
                                               hcl::ScalStreamBase&                  dmaStream,
                                               unsigned                              indexOfCg,
                                               uint64_t                              soAddressLSB,
                                               const std::vector<sibAddressAndSize>& sibAddressesAndSizes,
                                               ContextManager&                       contextManager,
                                               uint32_t                              fwStrideSize,
                                               uint64_t                              fwBaseAddress,
                                               uint8_t                               apiId);

    virtual void serializeGlobalDmaCommand(hcl::ScalStreamBase&                  scalStream,
                                           uint32_t                              soAddressLSB,
                                           const std::vector<sibAddressAndSize>& sibAddressesAndSizes,
                                           uint32_t                              fwStrideSize,
                                           uint64_t                              fwBaseAddress) override;

    void serializeScaleUpSendRecv(hcl::ScalStreamBase& scalStream,
                                  ContextManager&      contextManager,
                                  SendRecvAggregator&  aggregator,
                                  const SendRecvArray& sendRecvArray,
                                  int                  selfModuleId,
                                  HCL_Comm             comm,
                                  unsigned             collectiveContextIndex,
                                  uint32_t             soAddress,
                                  bool                 isSend,
                                  bool                 isLast,
                                  bool                 notifyRndvAck,
                                  bool                 waitForRndvAcks);

    // collectiveContextIndex = isSend * 8 + streamIndex;
    virtual void serializeScaleUpCollectiveOp(hcl::ScalStreamBase&   scalStream,
                                              ScaleUpCollectiveOpG2& scaleupCollectiveOp);

    virtual void serializeScaleOutCollectiveOp(hcl::ScalStreamBase&    scalStream,
                                               ScaleOutCollectiveOpG2& scaleoutCollectiveOp);

    void flushAggregator(hcl::ScalStreamBase&       scalStream,
                         SendRecvAggregator&        aggregator,
                         ContextManager&            contextManager,
                         unsigned                   collectiveContextIndex,
                         int                        selfModuleId,
                         HCL_Comm                   comm,
                         bool                       isSend,
                         edwords_t*                 pDwords,
                         RequiredCollectiveContext& collectiveContext,
                         bool                       notifyRndvAck,
                         bool                       waitForRndvAcks);

    virtual void
    serializeAllocBarrierCommand(hcl::ScalStreamBase&                                     scalStream,
                                 unsigned                                                 schedIdx,
                                 uint32_t                                                 completionGroupIndex,
                                 uint32_t                                                 requiredSobs,
                                 llvm_vecsmall::SmallVector<uint32_t, MAX_STREAM_TO_INC>* fences = nullptr) override;

    virtual void serializeLbwWriteCommand(hcl::ScalStreamBase& scalStream,
                                          unsigned             schedIdx,
                                          uint32_t             destination,
                                          uint32_t             data,
                                          bool                 blockUntilCompletion = false) override;

    virtual void serializeLbwBurstWriteCommand(hcl::ScalStreamBase&      scalStream,
                                               unsigned                  schedIdx,
                                               const LBWBurstDestData_t& destData,
                                               bool                      blockUntilCompletion = false) override;

    virtual void serializeLbwWriteWithFenceDecCommand(hcl::ScalStreamBase& scalStream,
                                                      unsigned             schedIdx,
                                                      uint32_t             destination,
                                                      uint32_t             data,
                                                      uint32_t             fenceIndex,
                                                      uint32_t             fenceTarget          = 1,
                                                      bool                 blockUntilCompletion = false) override;

    virtual void serializeFenceDecCommand(hcl::ScalStreamBase& scalStream,
                                          unsigned             schedIdx,
                                          uint32_t             fenceIndex,
                                          uint32_t             target = 1) override;

    virtual void
    serializeFenceIncCommand(hcl::ScalStreamBase& scalStream, unsigned schedIdx, uint32_t fenceIndex) override;

    virtual void serializeNopCommand(hcl::ScalStreamBase& scalStream, unsigned schedIdx, uint32_t padding) override;

    virtual void serializeNicNopCommand(pRecordWithMetadata& record,
                                        unsigned             collectiveContextIndex,
                                        uint32_t             dupMask,
                                        size_t               requiredCredits,
                                        unsigned             syncObjectAddressIndex,
                                        bool                 incSOB);
    void         serializeNicPassthroughCommand(hcl::ScalStreamBase&              scalStream,
                                                std::vector<pRecordWithMetadata>& records,
                                                size_t                            credits,
                                                bool                              isSend);

    size_t recordsSizeInDwords(std::vector<pRecordWithMetadata>& records);

    virtual void serializeUserSendCommand(std::vector<uint32_t>& out,
                                          unsigned               collectiveContextIndex,
                                          unsigned               commDescIndex,
                                          unsigned               syncObjectAddressIndex,
                                          uint32_t               cacheLineCount,
                                          uint32_t               cacheLineRemainder,
                                          uint8_t                elementRemainder,
                                          hcclDataType_t         dataType,
                                          uint64_t               address,
                                          bool                   isLastInGroup,
                                          bool                   notifyRndvAck,
                                          bool                   waitForRndvAcks);

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
                                      bool                 isFirstBufferUse = false) override;

    virtual void serializeSetTraceMarker(hcl::ScalStreamBase& scalStream, unsigned schedIdx, uint32_t val);

protected:
    virtual bool     isCastDown(uint32_t dmaType) override;
    virtual bool     isCastUp(uint32_t dmaType) override;
    virtual bool     isMemCpy(uint32_t dmaType) override;
    virtual unsigned getDmaTypeCastUp() override;
    virtual unsigned getDmaTypeCastDown() override;
    virtual unsigned getDmaTypeMemCpy() override;
};
