
#include "active_stream_manager.h"
#include "hcl_utils.h"
#include "collective_utils.h"
#include "infra/scal/gen2_arch_common/scal_stream.h"  // for ScalStream
#include "platform/gen2_arch_common/scaleout_provider.h"
#include "platform/gen2_arch_common/hcl_device_controller.h"

enum class CollectiveStreams
{
    RS = 0,
    AG = 1,
};

ActiveStreamManagerGen2Arch::ActiveStreamManagerGen2Arch(ScaleoutProvider*            scaleoutProvider,
                                                         HclDeviceControllerGen2Arch& deviceController,
                                                         unsigned                     archStreamIdx,
                                                         hcl::syncInfo&               longSo)
: m_deviceController(deviceController),
  m_scaleoutProvider(scaleoutProvider),
  m_archStreamIdx(archStreamIdx),
  m_longSo(longSo)
{
    for (auto sched = 0; sched < hcl::SchedulersIndex::count; sched++)
    {
        // resize the active streams per scheduler stream count
        auto streamCount =
            m_deviceController.getSchedulerMicroArchStreamCount(static_cast<hcl::SchedulersIndex>(sched));
        m_activeStreamsPerScheduler[static_cast<int>(sched)].resize(streamCount);
    }
}

void ActiveStreamManagerGen2Arch::initializeActiveStreams(CommonState& commonState, unsigned boxNum)
{
    m_commonState = &commonState;

    bool isHierarchicalSelfBox =
        (boxNum == m_commonState->m_dynamicComm.getMyScaleupGroup() && m_commonState->m_isMultiScaleupGroup);
    bool reductionRS = (m_commonState->m_currentOp == eHCLReduceScatter) && (!isHierarchicalSelfBox);
    bool isLastBox =
        (getNextBox(boxNum, m_commonState->m_boxIterations) == m_commonState->m_dynamicComm.getMyScaleupGroup());
    bool isFirstBox = boxNum == m_commonState->m_dynamicComm.getMyScaleupGroup();

    for (unsigned sched = 0; sched < hcl::SchedulersIndex::count; sched++)
    {
        // set all streams in lists to nullptr
        auto& streamList = m_activeStreamsPerScheduler[static_cast<int>(sched)];
        std::fill(streamList.begin(), streamList.end(), nullptr);

        // add the collective stream to the active stream list for each scaleup/scaleout scheduler
        if (sched != hcl::SchedulersIndex::gp)
        {
            addCollectiveStreamToActiveStreamList((hcl::SchedulersIndex)sched);
        }
    }

    // reduction
    bool isRedActive = isReductionStreamActive(reductionRS);
    if (isReductionStreamActive(reductionRS))
    {
        addStreamToActiveStreamList(HclStreamIndex::REDUCTION);
    }

    // scaleoutReduction
    if (isScaleoutReductionStreamActive(isLastBox))
    {
        addStreamToActiveStreamList(HclStreamIndex::SO_REDUCTION);
    }

    // signaling
    if (isSignalingStreamActive(isRedActive, isFirstBox))
    {
        addStreamToActiveStreamList(HclStreamIndex::SIGNALING);
    }

    if (isGdrStreamActive(isFirstBox))
    {
        addStreamToActiveStreamList(HclStreamIndex::GDR);
    }

    addStreamToActiveStreamList(HclStreamIndex::GC);
}

llvm_vecsmall::SmallVector<unsigned, MAX_STREAM_PER_SCHED>
ActiveStreamManagerGen2Arch::getActiveStreams(hcl::SchedulersIndex schedIdx) const
{
    llvm_vecsmall::SmallVector<unsigned, MAX_STREAM_PER_SCHED> activeStreams = {};
    const auto&                                                streamList    = m_activeStreamsPerScheduler[schedIdx];
    unsigned streamCount = m_deviceController.getSchedulerMicroArchStreamCount(schedIdx);
    for (unsigned i = 0; i < streamCount; i++)
    {
        if (streamList[i] != nullptr)
        {
            activeStreams.push_back(i);
        }
    }
    return activeStreams;
}

void ActiveStreamManagerGen2Arch::addStreamToActiveStreamList(HclStreamIndex hclStreamIdx)
{
    hcl::ScalStream& scalStream       = m_deviceController.getScalStream(m_archStreamIdx, hclStreamIdx);
    unsigned         uArchStreamIndex = scalStream.getUarchStreamIndex();
    unsigned         schedIdx         = scalStream.getSchedIdx();
    m_activeStreamsPerScheduler[schedIdx][uArchStreamIndex] = &scalStream;
    scalStream.setTargetValue(m_longSo.targetValue);
}

void ActiveStreamManagerGen2Arch::addCollectiveStreamToActiveStreamList(hcl::SchedulersIndex schedIdx)
{
    hcl::ScalStream& scalStream                             = getActiveCollectiveStream(schedIdx);
    unsigned         uArchStreamIndex                       = scalStream.getUarchStreamIndex();
    m_activeStreamsPerScheduler[schedIdx][uArchStreamIndex] = &scalStream;
    scalStream.setTargetValue(m_longSo.targetValue);
}

hcl::ScalStream* ActiveStreamManagerGen2Arch::getScalStreamIfNeeded(HclStreamIndex hclStreamIdx)
{
    hcl::ScalStream& scalStream       = m_deviceController.getScalStream(m_archStreamIdx, hclStreamIdx);
    unsigned         uArchStreamIndex = scalStream.getUarchStreamIndex();
    unsigned         schedIdx         = scalStream.getSchedIdx();
    return m_activeStreamsPerScheduler[schedIdx][uArchStreamIndex];
}

hcl::ScalStream& ActiveStreamManagerGen2Arch::getActiveCollectiveStream(hcl::SchedulersIndex schedIdx)
{
    hcl::ScalStream* scalStream = nullptr;
    switch (m_commonState->m_currentOp)
    {
        case eHCLGather:
        case eHCLAllGather:
        case eHCLSimpleBroadcast:
        case eHCLBroadcast:
        case eHCLSinglePeerBroadcast:
            scalStream = &m_deviceController.getScalAgUarchStream(m_archStreamIdx, schedIdx);
            break;
        case eHCLReduceScatter:
        case eHCLScatter:
        case eHCLReduce:
        case eHCLAllReduce:
        case eHCLAll2All:
        case eHCLNoCollective:  // used in Gen2Arch for Send/Recv operations
            scalStream = &m_deviceController.getScalRsUarchStream(m_archStreamIdx, schedIdx);
            break;
        default:
            VERIFY(false, "collective op is not supported {}", (int)m_commonState->m_currentOp);
    }
    return *scalStream;
}

hcl::ScalStream& ActiveStreamManagerGen2Arch::getArbitratorStream(const hcl::SchedulersIndex schedIdx)
{
    hcl::ScalStream& scalStream = m_deviceController.getScalArbUarchStream(m_archStreamIdx, schedIdx);
    scalStream.setTargetValue(m_longSo.targetValue);

    return scalStream;
}

bool ActiveStreamManagerGen2Arch::isReductionStreamActive(bool reductionRS)
{
    return ((m_commonState->m_16BitReduction || (!m_commonState->m_inPlace && !m_commonState->m_isMultiScaleupGroup)) &&
            reductionRS && m_commonState->m_dynamicComm.getScaleupGroupSize() != 1) ||
           (m_commonState->m_currentOp == eHCLReduceScatter && m_commonState->m_dynamicComm.getScaleupGroupSize() != 1);
}

bool ActiveStreamManagerGen2Arch::isScaleoutReductionStreamActive(bool isLastBox)
{
    return m_commonState->m_isMultiScaleupGroup && m_commonState->m_currentOp == eHCLReduceScatter &&
           (isLastBox || (m_commonState->isRSContReduction() && m_commonState->isBufferReductionIter()));
}

bool ActiveStreamManagerGen2Arch::isSignalingStreamActive(bool isReductionStreamActive, bool isFirstBox)
{
    bool isActive = false;
    if (m_commonState->m_currentOp == eHCLReduceScatter)
    {
        bool incLtu     = m_commonState->m_syncUpBufferWithLtu;
        bool isPdmaHnic = m_scaleoutProvider->isHostNic() && !m_scaleoutProvider->isGaudiDirect();
        isActive        = m_commonState->m_isMultiScaleupGroup &&
                   (((!isFirstBox && incLtu) && isReductionStreamActive) || (!isFirstBox && isPdmaHnic));
    }
    else if (m_commonState->m_currentOp == eHCLScatter)
    {
        bool isRootBox  = m_commonState->m_dynamicComm.getMyScaleupGroup() == m_commonState->rootBox();
        bool isPdmaHnic = m_scaleoutProvider->isHostNic() && !m_scaleoutProvider->isGaudiDirect();
        bool isPeersOnly =
            m_commonState->m_isMultiScaleupGroup && m_commonState->m_dynamicComm.getScaleupGroupSize() == 1;
        isActive = m_commonState->m_isMultiScaleupGroup && (!isFirstBox && isPdmaHnic && !isRootBox && !isPeersOnly);
    }

    VERIFY(isActive || !(m_commonState->m_syncUpBufferWithLtu && !isFirstBox) ||
               (m_commonState->m_currentOp != eHCLReduceScatter && m_commonState->m_currentOp != eHCLScatter),
           "signaling stream must be active when syncing with LTU!"
           "isActive={}, m_syncUpBufferWithLtu={}, m_currentOp={}",
           isActive,
           m_commonState->m_syncUpBufferWithLtu,
           m_commonState->m_currentOp);
    return isActive;
}

bool ActiveStreamManagerGen2Arch::isGdrStreamActive(bool isFirstBox)
{
    return m_scaleoutProvider->isGaudiDirect() && m_commonState->m_isMultiScaleupGroup &&
           m_commonState->m_currentOp == eHCLReduceScatter && !isFirstBox && !m_commonState->isRSContReduction();
}