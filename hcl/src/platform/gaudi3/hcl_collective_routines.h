#pragma once

#include <cstdint>  // for uint64_t

#include "hcl_api_types.h"                                      // for HCL_C...
#include "hcl_types.h"                                          // for MAX_R...
#include "platform/gen2_arch_common/hcl_collective_routines.h"  // for HclCo...
#include "platform/gen2_arch_common/types.h"                    // for GEN2A...
#include "platform/gen2_arch_common/collective_states.h"
#include "platform/gaudi3/send_recv_aggregator.h"             // for SendRecvAggregatorGaudi3
#include "platform/gaudi3/gaudi3_base_server_connectivity.h"  // for Gaudi3BaseServerConnectivity

class HclCommandsGaudi3;
class HclDeviceGaudi3;
class HclDynamicCommunicator;
class DeviceBufferManager;
class ScaleoutProvider;

namespace hcl
{
class ScalStreamBase;
}  // namespace hcl
struct SendRecvEntry;

class RemainderCalculatorGaudi3 : public RemainderCalculator
{
public:
    uint64_t getBufferClearSize(HCL_CollectiveOp collective,
                                uint64_t         originalSize,
                                e_devicePoolID   bufferId,
                                uint64_t         dataTypeSize,
                                uint64_t         scaleOutSendCount,
                                uint64_t         scaleOutRecvCount,
                                bool             isRoot,
                                bool             isBf16Reduction,
                                BoxNumInfo&      sendBoxNumInfo,
                                uint64_t         rootBox) override;
    uint64_t getBoxCount(uint64_t nonRemainderBoxCount,
                         uint64_t numBoxes,
                         uint64_t podSize,
                         uint64_t boxIndex,
                         uint64_t scaleUpCount,
                         uint64_t remainderCount) override;
    uint64_t getScaleOutCount(uint64_t nonRemainderScaleOutCount,
                              uint64_t numBoxes,
                              uint64_t boxCount,
                              uint64_t boxIndex,
                              uint64_t myRankInPod,
                              uint64_t scaleUpCount,
                              uint64_t remainderCount,
                              bool     lastRankInPod) override;
    uint64_t getDiv(uint64_t a, uint64_t b) override;
    uint64_t getRemainderCount(uint64_t totalCount, uint64_t scaleUpCount, uint64_t commSize) override;
    bool     isValidSlicing(uint32_t originalBufferCount,
                            uint32_t sliceCount,
                            uint64_t collectiveCount,
                            uint32_t numSlices,
                            uint32_t numRanks,
                            uint32_t minBufferCount) override;
    bool isSlicing(uint64_t totalCount, uint64_t totalCountPerRank, uint32_t bufferCount, uint32_t numRanks) override;
};

class HclCollectiveRoutinesGaudi3 : public HclCollectiveRoutinesGen2Arch
{
public:
    HclCollectiveRoutinesGaudi3(HclDeviceGaudi3* device, int streamId, WqeTracker* wqeTracker);
    virtual ~HclCollectiveRoutinesGaudi3();

    virtual void createScaleUpSendRecvOp(hcl::ScalStreamBase& scalStream,
                                         const SendRecvArray& sendRecvArray,
                                         int                  selfModuleId,
                                         HCL_Comm             comm,
                                         unsigned             collectiveContextIndex,
                                         uint32_t             soAddress,
                                         bool                 isSend,
                                         bool                 isLast,
                                         bool                 notifyRndvAck,
                                         bool                 waitForRndvAcks) override;

    // collectiveContextIndex = isSend * 8 + streamIndex;

    virtual void createScaleUpCollectiveOp(hcl::ScalStreamBase& scalStream, ScaleUpCollectiveOp& op_cmd) override;

    virtual void createScaleOutCollectiveOp(hcl::ScalStreamBase&  scalStream,
                                            ScaleOutCollectiveOp& scaleOutCollectiveOp) override;

    virtual unsigned countScaleUpSignalsSendRecv(CommonState&   commonState,
                                                 const uint32_t numberOfSendBuckets,
                                                 const uint32_t numberOfRecvBuckets,
                                                 const uint32_t numberOfSends,
                                                 const uint32_t numberOfRecvs,
                                                 const HCL_Comm comm) override;

    virtual unsigned countScaleOutSignalsSendRecv(const uint32_t numberOfSends,
                                                  const uint32_t numberOfRecvs,
                                                  const HCL_Comm comm) override;

    // we don't have to memset the buffers since we write for the first time and then perform reduction
    virtual void memsetIMBsIfNeeded(SliceState&      sendSliceState,
                                    SliceState&      recvSliceState,
                                    unsigned int     sizeInBytes,
                                    hcclDataType_t   dataType,
                                    hcl::ScalStream* garbageStream) override {};

    HclDeviceGaudi3& getDevice() { return *(reinterpret_cast<HclDeviceGaudi3*>(m_device)); }

private:
    HclCommandsGaudi3&            m_gaudi3Commands;
    Gaudi3BaseServerConnectivity& m_serverConnectivity;

    // different aggs for send/recv
    SendRecvAggregatorGaudi3 m_sendAggr;
    SendRecvAggregatorGaudi3 m_recvAggr;
};
