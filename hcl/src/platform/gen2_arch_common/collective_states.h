#pragma once

#include <cstddef>                  // for size_t
#include <cstdint>                  // for uint64_t, uint8_t
#include "hcl_api_types.h"          // for HCL_CollectiveOp
#include "hcl_collective_params.h"  // for HclCollectiveP...
#include "llvm/small_vector.h"      // for SmallVector
#include "platform/gen2_arch_common/signals/calculator.h"
#include "platform/gen2_arch_common/types.h"
#include "infra/scal/gen2_arch_common/scal_types.h"           // for HOST_FENCES_NR
#include "hcl_types.h"                                        // for HclConfigType
#include "platform/gen2_arch_common/device_buffer_manager.h"  // for e_devicePoolID

// fwd decl
class HclAddressGenerator;
class DeviceBufferManager;

class BoxNumInfo
{
public:
    enum boxOrientation
    {
        PREV_BOX      = 0,
        RECV_BOX      = PREV_BOX,
        NEXT_BOX      = 1,
        SEND_BOX      = NEXT_BOX,
        CURR_BOX_ITER = 2,
        MY_BOX        = 2,
    };

    BoxNumInfo(unsigned boxNum, boxOrientation orientation) : m_boxNum(boxNum), m_orientation(orientation) {};

    unsigned       m_boxNum;
    boxOrientation m_orientation;
};

class RemainderCalculator
{
public:
    virtual ~RemainderCalculator()                                                                    = default;
    virtual uint64_t getBufferClearSize(HCL_CollectiveOp collective,
                                        uint64_t         originalSize,
                                        e_devicePoolID   bufferId,
                                        uint64_t         dataTypeSize,
                                        uint64_t         scaleOutSendCount,
                                        uint64_t         scaleOutRecvCount,
                                        bool             isRoot,
                                        bool             isBf16Reduction,
                                        BoxNumInfo&      sendBoxNumInfo,
                                        uint64_t         rootBox)                                             = 0;
    virtual uint64_t getBoxCount(uint64_t nonRemainderBoxCount,
                                 uint64_t numBoxes,
                                 uint64_t ScaleupGroupSize,
                                 uint64_t boxIndex,
                                 uint64_t scaleUpCount,
                                 uint64_t remainderCount)                                             = 0;
    virtual uint64_t getScaleOutCount(uint64_t nonRemainderScaleOutCount,
                                      uint64_t numBoxes,
                                      uint64_t boxCount,
                                      uint64_t boxIndex,
                                      uint64_t myRankInScaleupGroup,
                                      uint64_t scaleUpCount,
                                      uint64_t remainderCount,
                                      bool     lastRankInScaleupGroup)                                    = 0;
    virtual uint64_t getDiv(uint64_t a, uint64_t b)                                                   = 0;
    virtual uint64_t getRemainderCount(uint64_t totalCount, uint64_t scaleUpCount, uint64_t commSize) = 0;
    virtual bool     isValidSlicing(uint32_t originalBufferCount,
                                    uint32_t sliceCount,
                                    uint64_t collectiveCount,
                                    uint32_t numSlices,
                                    uint32_t numRanks,
                                    uint32_t minBufferCount)                                          = 0;
    virtual bool
    isSlicing(uint64_t totalCount, uint64_t totalCountPerRank, uint32_t bufferCount, uint32_t numRanks) = 0;
};

class CommonState : public HclCollectiveParams
{
public:
    explicit CommonState(HclCollectiveParams& other,
                         DeviceBufferManager& intermediateBufferManager,
                         bool                 isHostNic,
                         bool                 isGdr,
                         unsigned             workDistributionGroupSize,
                         const unsigned       maxNumScaleUpPortsPerConnection,
                         unsigned             numScaleOutPorts,
                         SignalsCalculator&   signalsCalculator,
                         RemainderCalculator* remainderCalculator);

    void     calcMaxSliceCounts();
    void     calcSliceCounts(unsigned sliceIter);
    uint32_t getNumSlices(uint64_t totalRankCount, uint32_t numRanks);

    void initCollectiveOp(const bool singlePeerBroadcastAllowed);
    void initCurrentOp(HCL_CollectiveOp currentOp, unsigned boxIter, unsigned all2allIter);
    void checkInPlaceOp();
    void setIsReductionCollective();
    void check16BitReductionOp();
    void calcScaleoutLongterm();
    void determineSyncUpBufferWithLtu();

    void checkHierarchicalOp();

    bool     isRemainderAllowedForCollective() const;
    bool     isComplexImplementation() const;
    bool     isRoot() const;
    bool     isRootPeerInclusive(HCL_Rank rank) const;
    bool     isRootPeerExclusive(HCL_Rank rank) const;
    bool     isRootOrRootPeer() const;
    bool     isRootPeer() const;
    unsigned rootBox() const;
    bool     isRootBox() const { return m_isRootBox; }
    bool     isLastBox(BoxNumInfo& boxNumInfo) const;
    bool     isLastSlice(unsigned iterNum) const;
    bool     isHostNic() const;
    bool     isGDR() const;

    virtual void calcSliceQpSet(const unsigned sliceIter);

    unsigned countSignalsSingleOp() const;

    uint64_t getIntermediateBuffer(e_devicePoolID poolIndex);

    const uint64_t m_hnicQpSprayThreshold;
    uint64_t       m_rankScaleUpCount;
    uint64_t       m_scaleUpStrideCount;
    uint64_t       m_boxCount;
    uint64_t       m_rankScaleOutCount;
    uint64_t       m_boxStrideCount;
    uint64_t       m_sliceOffsetCount;
    uint64_t       m_optimalBufferCount;
    uint64_t       m_remainderCount = 0;
    uint64_t       m_submitCounter  = 0;
    unsigned       m_boxIterations  = 0;
    unsigned       m_rootBox        = 0;

    unsigned m_all2allIterations      = 1;
    uint64_t m_all2allIterStrideCount = 0;

    bool     m_inPlace                = false;
    bool     m_16BitReduction         = false;
    bool     m_isMultiScaleupGroup    = false;
    bool     m_hasBufferSize          = false;
    bool     m_isReductionCollective  = false;
    bool     m_isSlicing              = false;
    bool     m_isRoot                 = false;
    bool     m_isRootPeer             = false;
    bool     m_isRootBox              = false;
    bool     m_isHostNic              = false;
    bool     m_isGdr                  = false;
    size_t   m_sliceIterations        = 0;
    unsigned m_scaleoutBuffersAmount  = DeviceBufferManager::getFactor(SCALEOUT_POOL);
    unsigned m_scaleoutLongtermAmount = DeviceBufferManager::getFactor(SCALEOUT_POOL) + 1;

    unsigned m_boxIter                   = 0;
    unsigned m_all2allIter               = 0;
    unsigned m_workDistributionGroupSize = 0;
    unsigned m_numScaleOutPorts          = 0;

    unsigned m_dataTypeSizeInBytes = 0;
    bool     m_syncUpBufferWithLtu = false;

    uint8_t m_qpSet = 0;

    DeviceBufferManager& m_intermediateBufferManager;
    RemainderCalculator* m_remainderCalculator;

    // for hnics scaleout send/recv only - needs somehow to be moved down cast class
    uint64_t m_scaleoutNonCollectiveSend = 0;
    uint64_t m_scaleoutNonCollectiveRecv = 0;

    uint64_t getAddressOffset(unsigned iterNum);
    uint64_t getChunkCount();
    uint64_t getChunkCountToClear();
    uint64_t getStrideCount();

    uint64_t getSendAddress(unsigned iterNum);
    uint64_t getRecvAddress(unsigned iterNum);
    uint8_t  getQpSet();

    inline unsigned signalToCost(enum SignalEvent signal) { return m_signalsCalculator->signalToCost(signal); }

    bool     isLongtermGPSORequired(const unsigned boxIter);
    unsigned calcLongtermContinuousTarget(const unsigned boxIter);
    bool     isScaleoutRequired(bool isSend, BoxNumInfo& sendBoxNumInfo);

    unsigned getNumScaleOutPorts();
    uint64_t calcSendAddrSize() const;
    uint64_t calcRecvAddrSize() const;
    bool     isSendAddrValid() const;
    bool     isRecvAddrValid() const;

    void initializeSignalsCalculator();

    bool     isEdgeIteration(BoxNumInfo& boxNumInfo) const;
    bool     isEdgeIteration() const;
    unsigned calcBoxIterRecv(BoxNumInfo& boxNumInfo) const;

    unsigned getBroadcastScatterOpBoxIterations() const;
    uint64_t calculateCUID(bool isFirstBox, bool isLastBox);

private:
    HclConfigType      m_boxType;
    const uint32_t     m_maxNumScaleUpPortsPerConnection;
    SignalsCalculator* m_signalsCalculator;
};

struct SliceExecutionOutput
{
    uint64_t m_deviceCount   = 0;
    uint64_t m_strideCount   = 0;
    uint64_t m_cellCount     = 0;
    uint64_t m_deviceAddress = 0;

    // We requested 'm_setup.m_scaleoutInternalSOBs' sync objects. These are the actual SOBs.
    llvm_vecsmall::SmallVector<sob_info, 16> m_scaleoutInternalSOBs;

    // We requested 'm_setup.m_scaleoutInternalFences' fences. These are the actual fences.
    llvm_vecsmall::SmallVector<fence_info, HOST_FENCES_NR> m_scaleoutFences;

    // These variables determine on which WaitEvent and on which WaitMethod the scaleout will signal to.
    WaitEvent  m_scaleoutCompletionWaitEvent  = WaitEvent::WAIT_EVENT_MAX;
    WaitMethod m_scaleoutCompletionWaitMethod = WaitMethod::WAIT_METHOD_MAX;

    // Once the scaleout provider finishes, increments should be made to this
    // SOB index. If m_scaleoutSignalToGPSO = true, this won't equal the CGSO but rather a GPSO.
    uint32_t m_completionSoAddr = 0;
};

struct SliceSetupOutput
{
    // llvm_vecsmall::SmallVector<sib_types, 16> m_staticIntermediateTypes;

    // This indicates how many internal SOBs is the scaleout provider requesting for scaleout internal use.
    uint8_t m_scaleoutInternalSOBs = 0;

    // This indicates how many internal fences is the scaleout provider requesting for scaleout internal use.
    uint8_t m_scaleoutInternalFences = 0;

    // This indicates to which SignalEvent the scaleout provider will signal to, to notify its' completion.
    SignalEvent m_scaleoutCompletionWaitSignal = SignalEvent::SIGNAL_EVENT_MAX;
};

struct SliceState : public CommonState
{
public:
    explicit SliceState(const CommonState&   commonState,
                        HclAddressGenerator& addressGenerator,
                        HCL_CollectiveOp     currentOp,
                        bool                 isSend,
                        unsigned             sliceIter,
                        BoxNumInfo           boxNumInfo,
                        int                  streamId);

    void calcBoxAndScaleOutCounts();
    bool gatherOpsWaitForRS(bool isScaleup);

    bool       m_isSend;
    unsigned   m_sliceIter;
    BoxNumInfo m_boxNumInfo;

    bool m_isHierarchicalFirst = false;
    bool m_isHierarchicalLast  = false;

    struct SliceSetupOutput m_setup;

    struct SliceExecutionOutput m_execution;
};

class NonCollectiveState : public CommonState
{
public:
    explicit NonCollectiveState(const CommonState&   commonState,
                                HclAddressGenerator& addressGenerator,
                                const bool           isSend,
                                const uint32_t       completionSoAddr,
                                const bool           isAnyScaleoutRequired);

    struct SliceSetupOutput     m_setup;
    struct SliceExecutionOutput m_execution;
    const bool                  m_isSend;            // does not change
    const uint32_t              m_completionSoAddr;  // does not change between calls
    HclAddressGenerator&        m_addressGenerator;
    unsigned                    m_remoteBox      = 0;
    HCL_Rank                    m_remoteRank     = HCL_INVALID_RANK;
    unsigned int                m_recvFenceValue = 0;  // initialized per number of ranks in same recv
    bool                        m_firstRank      = false;
    const bool                  m_isScaleoutRequired;  // does not change between calls
    uint64_t                    m_hostMappedAddr = 0;  // for hnics scaleout
    uint64_t                    m_hostAddr       = 0;  // for hnics scaleout

    void updateState(const unsigned       remoteBox,
                     const HCL_Rank       remoteRank,
                     const hcclDataType_t dataType,
                     const uint64_t       deviceAddress,
                     const uint64_t       count,
                     const bool           firstRank,
                     const unsigned int   recvFenceValue,
                     const uint64_t       hostMappedAddr,  // for hnics scaleout
                     const uint64_t       hostAddr);             // for hnics scaleout

    bool         isScaleOutRequired() const;
    virtual void calcSliceQpSet(const unsigned sliceIter) override final;
};
