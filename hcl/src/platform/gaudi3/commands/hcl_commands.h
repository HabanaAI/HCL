#pragma once

#include "platform/gen2_arch_common/commands/hcl_commands.h"  // for HclComm...

#include <cstdint>  // for uint32_t
#include <array>    // for array
#include <utility>  // for pair
#include <vector>   // for vector

#include "hcl_api_types.h"                            // for HCL_Col...
#include "platform/gen2_arch_common/types.h"          // for GEN2ARC...
#include "platform/gaudi3/send_recv_aggregator.h"     // for SendRecvArray
#include "platform/gaudi3/nic_passthrough_handler.h"  // for pRecordWithMetadataGaudi3

class HclDeviceGen2Arch;
class SendRecvAggregatorGaudi3;

namespace hcl
{
class ScalStreamBase;
}

struct DmaCmdParamsG3 : public DmaCmdParams
{
    explicit DmaCmdParamsG3(DmaCmdParams& other) : DmaCmdParams(other) {};

    uint64_t m_sendDataSize;
    bool     m_isReduction;
    uint32_t m_tempDmaType;
};

struct ScaleUpCollectiveOpG3 : public ScaleUpCollectiveOp
{
    explicit ScaleUpCollectiveOpG3(ScaleUpCollectiveOp& other) : ScaleUpCollectiveOp(other) {}

    explicit ScaleUpCollectiveOpG3(box_devices_t& deviceToRemoteIndex, HclDynamicCommunicator& dynamicComm)
    : ScaleUpCollectiveOp(dynamicComm, deviceToRemoteIndex)
    {
    }

    uint8_t  m_dcore;
    uint8_t  m_ssm;
    uint16_t m_sobId;
    uint32_t m_ScaleupGroupSize;
    uint32_t m_qpn;
    bool     m_disregardRank;
    uint32_t m_ports_mask;
};

struct ScaleOutCollectiveOpG3 : public ScaleOutCollectiveOp
{
    explicit ScaleOutCollectiveOpG3(ScaleOutCollectiveOp& other) : ScaleOutCollectiveOp(other) {}

    uint8_t  m_dcore;
    uint8_t  m_ssm;
    uint16_t m_sobId;
    uint32_t m_ScaleupGroupSize;
    uint32_t m_qpn;
    bool     m_disregardRank;
    uint32_t m_ports_mask;
    uint32_t m_lagSize;
};

class HclCommandsGaudi3 : public HclCommandsGen2Arch
{
public:
    HclCommandsGaudi3();
    HclCommandsGaudi3(HclCommandsGaudi3&&)                 = delete;
    HclCommandsGaudi3(const HclCommandsGaudi3&)            = delete;
    HclCommandsGaudi3& operator=(HclCommandsGaudi3&&)      = delete;
    HclCommandsGaudi3& operator=(const HclCommandsGaudi3&) = delete;
    virtual ~HclCommandsGaudi3()                           = default;

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

    void serializeUpdateNicOffsets(hcl::ScalStreamBase&                     scalStream,
                                   bool                                     isSend,
                                   bool                                     isScaleUp,
                                   uint32_t                                 qpn,
                                   std::array<uint16_t, MAX_NICS_GEN2ARCH>& remoteIndices);

    virtual void serializeUpdateLastRank(hcl::ScalStreamBase& scalStream,
                                         bool                 isSend,
                                         bool                 isScaleUp,
                                         uint32_t             qpn,
                                         uint32_t             ports_mask);

    void serializeScaleUpSendRecv(hcl::ScalStreamBase&               scalStream,
                                  const int                          selfModuleId,
                                  const bool                         isSend,
                                  const uint8_t                      dcore,
                                  const uint8_t                      ssm,
                                  const uint16_t                     sobId,
                                  const uint32_t                     qpn,
                                  const SendRecvArray&               sendRecvArray,
                                  const RemoteDevicePortMasksVector& remoteDevicesPortMasks,
                                  const HCL_Comm                     comm,
                                  SendRecvAggregatorGaudi3&          sendRecvAggr,
                                  const unsigned                     maxNumScaleUpNicsPerConnection);

    // Serialize a single device send/recv
    void serializeScaleUpSendRecvDevice(hcl::ScalStreamBase& scalStream,
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
                                        const unsigned       maxNumScaleUpNicsPerConnection);

    // Serialize a single rank s/r into buffer, does not send to chip
    void serializeScaleUpSendRecvDeviceCmd(const bool             isSend,
                                           const uint32_t         qpn,
                                           const uint64_t         buff,
                                           const uint64_t         count,
                                           const uint8_t          dcore,
                                           const uint8_t          ssm,
                                           const uint16_t         sobId,
                                           const uint32_t         ports_mask,
                                           const hcclDataType_t   dataType,
                                           const unsigned         maxNumScaleUpNicsPerConnection,
                                           std::vector<uint32_t>& dwordsBuffer /* output */);

    static void serializeNicPassthroughCommand(hcl::ScalStreamBase&             scalStream,
                                               const bool                       isSend,
                                               const uint32_t                   credits,
                                               const pRecordWithMetadataGaudi3& record);

    static void serializeNicNopCommand(hcl::ScalStreamBase& scalStream,
                                       const bool           isSend,
                                       const uint16_t       dupMask,
                                       const uint32_t       credits,
                                       const uint32_t       consumeDwords);

    virtual void serializeScaleUpCollectiveOp(hcl::ScalStreamBase&   scalStream,
                                              ScaleUpCollectiveOpG3& scaleupCollectiveOp,
                                              const unsigned         maxNumScaleUpNicsPerConnection);

    virtual void serializeScaleOutCollectiveOp(hcl::ScalStreamBase&    scalStream,
                                               ScaleOutCollectiveOpG3& scaleoutCollectiveOp);

    virtual void
    serializeAllocBarrierCommand(hcl::ScalStreamBase&                                        scalStream,
                                 unsigned                                                    schedIdx,
                                 uint32_t                                                    completionGroupIndex,
                                 uint32_t                                                    requiredSobs,
                                 llvm_vecsmall::SmallVector<uint32_t, MAX_STREAM_PER_SCHED>* fences = nullptr,
                                 const LBWBurstData_t* destData                                     = nullptr) override;

    virtual void serializeLbwWriteCommand(hcl::ScalStreamBase& scalStream,
                                          unsigned             schedIdx,
                                          uint32_t             destination,
                                          uint32_t             data) override;

    virtual void serializeLbwWriteWithFenceDecCommand(hcl::ScalStreamBase& scalStream,
                                                      unsigned             schedIdx,
                                                      uint32_t             destination,
                                                      uint32_t             data,
                                                      uint32_t             fenceIndex,
                                                      uint32_t             fenceTarget = 1) override;

    virtual void serializeLbwBurstWriteCommand(hcl::ScalStreamBase&  scalStream,
                                               unsigned              schedIdx,
                                               const LBWBurstData_t& destData) override;

    virtual void serializeFenceDecCommand(hcl::ScalStreamBase& scalStream,
                                          unsigned             schedIdx,
                                          uint32_t             fenceIndex,
                                          uint32_t             target = 1) override;

    virtual void
    serializeFenceIncCommand(hcl::ScalStreamBase& scalStream, unsigned schedIdx, uint32_t fenceIndex) override;

    virtual void serializeNopCommand(hcl::ScalStreamBase& scalStream, unsigned schedIdx, uint32_t padding) override;

    virtual void
    serializeGlobalDmaCommand(hcl::ScalStreamBase&                                 scalStream,
                              unsigned                                             schedIdx,
                              uint32_t                                             soAddressLSB,
                              const std::vector<SimbPoolContainerParamsPerStream>& containerParamsPerStreamVec,
                              uint32_t                                             fwStrideSize,
                              uint64_t                                             fwBaseAddress) override;

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

    virtual void serializeSetTraceMarker(hcl::ScalStreamBase& scalStream, unsigned schedIdx, uint32_t val) override;
};
