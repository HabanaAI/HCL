#pragma once

#include "interfaces/hcl_icollective_routines.h"

#include <cstddef>  // for NULL
#include <cstdint>
#include <array>
#include <map>  // for map
#include <mutex>
#include <vector>
#include "hcl_api_types.h"                                    // for HCL_Comm, HCL_CollectiveOp
#include "platform/gen2_arch_common/group_calls.h"            // for GroupCallsBuckets, SendRecvVector
#include "platform/gen2_arch_common/hcl_address_generator.h"  // for HclAddressGenerator
#include "llvm/small_vector.h"                                // for SmallVector
#include "hcl_types.h"
#include "platform/gen2_arch_common/types.h"          // for GEN2ARCH_HLS_BOX_SIZE
#include "intermediate_buffer_container.h"            // for IntermediateBuffersAmount
#include "platform/gen2_arch_common/signals/types.h"  // for WaitEvent
#include "platform/gen2_arch_common/wqe_tracker.h"    // for WqeWraparoundBits
#include "platform/gen2_arch_common/collective_states.h"
#include "platform/gen2_arch_common/commands/hcl_commands.h"
#include "platform/gen2_arch_common/hcl_device_controller.h"
#include "platform/gen2_arch_common/hcl_mem_handler.h"
#include "platform/gen2_arch_common/server_connectivity.h"  // for Gen2ArchServerConnectivity
#include "platform/gen2_arch_common/active_stream_manager.h"
#include "collective_interface/prims/hccl_prim.h"
#include "collective_interface/hccl_graph.h"

#include "buffer_allocation_manager.h"

class Gen2ArchScalUtils;
class HclCommandsGen2Arch;
class HclDeviceGen2Arch;
class HclDeviceControllerGen2Arch;
class HclCollectiveMemHandlerGen2Arch;
class HclDynamicCommunicator;
class UniqueSortedVector;
class SignalsManager;
class DependencyChecker;

namespace hcl
{
class ScalStream;
class ScalStreamBase;
}  // namespace hcl

class CommonState;
class NonCollectiveState;
struct SliceState;
struct HclCollectiveParams;
struct SendRecvMemCopyEntry;
class ScaleoutProvider;
class DeviceBufferManager;
class HclGraphSyncGen2Arch;

class HclCollectiveRoutinesGen2Arch : public IHcclGraphEngine
{
public:
    HclCollectiveRoutinesGen2Arch(HclDeviceGen2Arch* device, int streamId, WqeTracker* wqeTracker);
    virtual ~HclCollectiveRoutinesGen2Arch();
    virtual uint64_t     initGraph(HcclGraph* graph) override;
    virtual void         finalizeGraph(HcclGraph* graph, uint64_t startTargetVal) override;
    virtual void         initExec(HcclGraph* graph, int exec) override;
    virtual void         finalizeExec(HcclGraph* graph, int exec) override;
    virtual hcclResult_t processAgPrim(HcclGraph* graph, HcclPrimAllGather* agPrim) override;
    virtual hcclResult_t processBcastPrim(HcclGraph* graph, HcclPrimBroadcast* bcastPrim) override;
    virtual hcclResult_t processSendPrim(HcclGraph* graph, HcclPrimSend* sendPrim) override;
    virtual hcclResult_t processRecvPrim(HcclGraph* graph, HcclPrimRecv* recvPrim) override;

    void barrierArmSchedulers(unsigned requiredCredits, HCL_CollectiveOp currentOp);
    void configureExternalSoForCompletion(unsigned numSignals);

    void                 onCommInit(const HCL_Comm commId);
    virtual hcclResult_t hclCollectiveCall(HclCollectiveParams& params);
    virtual void         hclCollectiveCall(CommonState&     commonState,
                                           unsigned         sliceIter,
                                           unsigned         boxIter,
                                           unsigned         all2allIter,
                                           const unsigned   firstBoxIter,
                                           bool             isFirstOp,
                                           HCL_CollectiveOp currentOp = eHCLCollectiveLastValue);

    hcclResult_t sendRecv(hcl::GroupCallsBuckets&            groupCallsBuckets,
                          std::vector<SendRecvMemCopyEntry>& sendRecvMemCpyVec,
                          HCL_Comm                           comm,
                          const std::set<HCL_Rank>&          remoteRanks,
                          uint8_t                            apiId);

    int getRemoteRankToRsi(CommonState& commonState, bool isSend, HCL_Rank remoteRank, bool isAllGatherQp);

    virtual void createScaleOutCollectiveOp(hcl::ScalStreamBase&  scalStream,
                                            ScaleOutCollectiveOp& scaleOutCollectiveOp) = 0;

    void streamAddSingleWaitIfNeeded(hcl::ScalStream&                           scalStream,
                                     llvm_vecsmall::SmallVector<WaitEvent, 8>&& waitEvents);

    uint64_t checkSendRecvDependency(uint64_t address,
                                     uint64_t size,
                                     uint64_t targetValue,
                                     bool     isSend,
                                     bool     dbModificationIsAllowed = true);
    uint64_t
    checkCollectiveDependency(CommonState& commonState, uint64_t targetValue, bool dbModificationIsAllowed = true);

    uint32_t getSoConfigValue(unsigned value, bool isReduction);

    HclDeviceControllerGen2Arch& m_deviceController;

    Gen2ArchScalUtils* getScalUtils() { return m_utils; }
    SignalsManager*    getSignalsManager() { return m_signalsManager; }
    uint64_t           getCurrentTargetValue() { return m_longSo.targetValue; }
    int                getArchStream() { return m_streamId; }

    void setGroupContext(const bool value);
    bool getGroupContext() const { return m_groupContext; }

    WqeWraparoundBits    getWraparoundBits(HCL_Comm commId, unsigned rank, QpType qpType);
    DeviceBufferManager& getIntermediateBufferManager() { return m_intermediateBufferManager; }
    HclDeviceGen2Arch*   getDevice() { return m_device; }
    uint64_t             getGroupMaxTargetValue() const { return m_groupMaxTargetValue; }
    void                 setGroupMaxTargetValue(uint64_t targetVal) { m_groupMaxTargetValue = targetVal; }
    RemainderCalculator* m_remainderCalculator = nullptr;

protected:
    virtual void initCollectiveRoutinesGen2Arch();
    void         createDmaProgs(SliceState&    sendSliceState,
                                SliceState&    recvSliceState,
                                unsigned int   sizeInBytes,
                                unsigned       requiredCredits,
                                hcclDataType_t dataType);

    void createScaleUpSendProgs(SliceState&      sliceState,
                                unsigned         sliceIter,
                                BoxNumInfo&      boxNumInfo,
                                unsigned         requiredCredits,
                                HCL_CollectiveOp currentOp,
                                unsigned         numSignals);

    void createScaleUpRecvProgs(SliceState&      sliceState,
                                unsigned         sliceIter,
                                BoxNumInfo&      boxNumInfo,
                                unsigned         requiredCredits,
                                HCL_CollectiveOp currentOp);

    void createDmaProgsNonCollective(unsigned int sizeInBytes, unsigned requiredCredits);

    void createScaleUpSendProgsNonCollective(uint32_t                                 numberOfSendBuckets,
                                             uint32_t                                 numberOfRecvBuckets,
                                             uint32_t                                 numberOfSends,
                                             uint32_t                                 numberOfRecvs,
                                             const SendRecvArray&                     sendVec,
                                             const std::vector<SendRecvMemCopyEntry>& memcopyVec,
                                             unsigned                                 scaleoutSignals,
                                             HCL_Comm                                 comm,
                                             unsigned                                 requiredCredits,
                                             uint8_t                                  apiId);

    void createScaleUpRecvProgsNonCollective(uint32_t             numberOfRecv,
                                             const SendRecvArray& recvVec,
                                             HCL_Comm             comm,
                                             unsigned             requiredCredits);

    void createScaleOutSendProgsNonCollective(const SendRecvVector&                   sendVec,
                                              const HCL_Comm                          comm,
                                              const unsigned                          requiredCredits,
                                              std::unordered_map<HCL_Rank, unsigned>& qpSetIterPerSendPeerRank,
                                              const CommonState&                      commonState);

    void createScaleOutRecvProgsNonCollective(const SendRecvVector&                   recvVec,
                                              const HCL_Comm                          comm,
                                              const unsigned                          requiredCredits,
                                              std::unordered_map<HCL_Rank, unsigned>& qpSetIterPerRecvPeerRank,
                                              const CommonState&                      commonState);

    void createScaleOutSendProgs(SliceState& sliceState, unsigned requiredCredits);

    void createScaleOutRecvProgs(SliceState& sliceState, unsigned requiredCredits);

    void getDeviceToRemoteIndex(CommonState&   commonState,
                                bool           isSend,
                                box_devices_t& deviceToRemoteIndex,
                                bool           isAllGatherQp = false);

    void advanceProg(bool nopOp, uint64_t cuid, CommonState* commonState = nullptr);

    void calculateScaleupSignals(CommonState& commonState,
                                 BoxNumInfo&  boxNumInfo,
                                 bool         isLastBox  = false,
                                 bool         isFirstBox = false);

    void calculateScaleupSignalsReduceScatter(CommonState& commonState, bool isLastBox, bool isFirstBox);
    void calculateScaleupSignalsReduceScatterForOtherOps(CommonState& commonState, bool isLastBox, bool isFirstBox);

    unsigned int calcRequiredCreditAmount(CommonState&     commonState,
                                          BoxNumInfo&      nexBoxNumInfo,
                                          BoxNumInfo&      prevBoxNumInfo,
                                          unsigned         sliceIter,
                                          unsigned         boxIter,
                                          const unsigned   firstBoxIter,
                                          bool             isFirstOp,
                                          HCL_CollectiveOp currentOp,
                                          int64_t          dependencyTargetVal);

    virtual void createScaleUpSendRecvOp(hcl::ScalStreamBase& scalStream,
                                         const SendRecvArray& sendRecvArray,
                                         int                  selfModuleId,
                                         HCL_Comm             comm,
                                         unsigned             collectiveContextIndex,
                                         uint32_t             soAddress,
                                         bool                 isSend,
                                         bool                 isLast,
                                         bool                 notifyRndvAck,
                                         bool                 waitForRndvAcks) = 0;

    // collectiveContextIndex = isSend * 8 + streamIndex;

    virtual void createScaleUpCollectiveOp(hcl::ScalStreamBase& scalStream, ScaleUpCollectiveOp& op_cmd) = 0;

    void             determineCompletionSO(SliceState& sliceState, bool isFirstBox, bool isLastBox);
    void             provideScaleoutResources(SliceState& sliceState);
    void             provideScaleoutResources(NonCollectiveState& nonCollectiveState);
    void             negotiateScaleoutResources(SliceState& sliceState, bool isFirstBox, bool isLastBox);
    virtual unsigned countScaleUpSignalsSendRecv(CommonState&   commonState,
                                                 const uint32_t numberOfSendBuckets,
                                                 const uint32_t numberOfRecvBuckets,
                                                 const uint32_t numberOfSends,
                                                 const uint32_t numberOfRecvs,
                                                 const HCL_Comm comm) = 0;

    virtual unsigned
    countScaleOutSignalsSendRecv(const uint32_t numberOfSends, const uint32_t numberOfRecvs, const HCL_Comm comm) = 0;

    void syncWithLtuIfNeeded(SliceState& sliceState, hcl::ScalStream& scalStream);

    virtual void memsetIMBsIfNeeded(SliceState&      sendSliceState,
                                    SliceState&      recvSliceState,
                                    unsigned int     sizeInBytes,
                                    hcclDataType_t   dataType,
                                    hcl::ScalStream* garbageStream) = 0;

    void addScaleoutInternalSOB(SliceState& sliceState, WaitMethod method);

    HclDeviceGen2Arch*    m_device;
    int                   m_streamId = 0;
    HclGraphSyncGen2Arch& m_graphSync;

    hcl::syncInfo m_longSo;
    hcl::syncInfo m_longSoNullSubmit;

    HclConfigType m_boxType;

    BufferAllocationManager m_staticBuffersAllocator;
    DeviceBufferManager&    m_intermediateBufferManager;
    Gen2ArchScalUtils*      m_utils = NULL;

    HclCommandsGen2Arch&                             m_commands;
    std::unique_ptr<HclCollectiveMemHandlerGen2Arch> m_memHandler;

    ScaleoutProvider*           m_scaleoutProvider;
    ActiveStreamManagerGen2Arch m_activeStreamManager;

    WqeTracker* m_wqeTracker = nullptr;

    SignalsManager*                      m_signalsManager = nullptr;
    std::unique_ptr<DependencyChecker>   m_dependencyChecker;
    std::unique_ptr<HclAddressGenerator> m_addressGenerator = nullptr;

    bool                              m_groupContext            = false;
    bool                              m_groupContextStrongOrder = false;
    uint64_t                          m_groupMaxTargetValue     = 0;
    std::vector<e_devicePoolID>       m_memset_buffers          = {SCALEOUT_POOL, REDUCE_POOL};
    const Gen2ArchServerConnectivity& m_serverConnectivity;
    bool                              m_nullSubmit;

    bool m_requestStrongOrderIter = false;
};
