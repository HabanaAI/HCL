
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
}

void ActiveStreamManagerGen2Arch::initializeDmaStreams(CommonState& commonState, unsigned boxNum)
{
    HCL_CollectiveOp currentOp = commonState.m_currentOp;
    bool             isActive  = false;
    unsigned         schedIdx  = (unsigned)hcl::SchedulersIndex::dma;

    m_commonState = &commonState;

    bool isHierarchicalSelfBox =
        (boxNum == commonState.m_dynamicComm.getMyScaleupGroup() && commonState.m_isMultiScaleupGroup);
    bool reductionRS     = (currentOp == eHCLReduceScatter) && (!isHierarchicalSelfBox);
    bool reductionReduce = false;
    bool isLastBox = (getNextBox(boxNum, commonState.m_boxIterations) == commonState.m_dynamicComm.getMyScaleupGroup());
    bool isFirstBox = boxNum == commonState.m_dynamicComm.getMyScaleupGroup();

    m_dmaStreams.resize(static_cast<size_t>(hcl::DMAStreams::max));
    std::fill(m_dmaStreams.begin(), m_dmaStreams.end(), nullptr);

    // arbitrator
    fillDmaStream(hcl::DMAStreams::arbitrator, m_archStreamIdx, schedIdx);

    // garbageCollection
    fillDmaStream(hcl::DMAStreams::garbageCollection, m_archStreamIdx, schedIdx);

    // reduction
    isActive = ((commonState.m_16BitReduction || (!commonState.m_inPlace && !commonState.m_isMultiScaleupGroup)) &&
                reductionRS && commonState.m_dynamicComm.getScaleupGroupSize() != 1) ||
               reductionReduce ||
               (currentOp == eHCLReduceScatter && commonState.m_dynamicComm.getScaleupGroupSize() != 1);
    bool isReductionStreamActive = isActive;
    if (isActive) fillDmaStream(hcl::DMAStreams::reduction, m_archStreamIdx, schedIdx);

    // scaleoutReduction
    isActive = commonState.m_isMultiScaleupGroup && currentOp == eHCLReduceScatter &&
               (isLastBox || (commonState.isRSContReduction() && commonState.isBufferReductionIter()));
    if (isActive) fillDmaStream(hcl::DMAStreams::scaleoutReduction, m_archStreamIdx, schedIdx);

    // signaling
    isActive = false;
    if (currentOp == eHCLReduceScatter)
    {
        bool incLtu     = commonState.m_syncUpBufferWithLtu;
        bool isPdmaHnic = m_scaleoutProvider->isHostNic() && !m_scaleoutProvider->isGaudiDirect();
        isActive        = commonState.m_isMultiScaleupGroup &&
                   (((!isFirstBox && incLtu) && isReductionStreamActive) || (!isFirstBox && isPdmaHnic));
    }
    else if (currentOp == eHCLScatter)
    {
        bool isRootBox   = commonState.m_dynamicComm.getMyScaleupGroup() == commonState.rootBox();
        bool isPdmaHnic  = m_scaleoutProvider->isHostNic() && !m_scaleoutProvider->isGaudiDirect();
        bool isPeersOnly = commonState.m_isMultiScaleupGroup && commonState.m_dynamicComm.getScaleupGroupSize() == 1;
        isActive = commonState.m_isMultiScaleupGroup && (!isFirstBox && isPdmaHnic && !isRootBox && !isPeersOnly);
    }

    VERIFY(isActive || !(commonState.m_syncUpBufferWithLtu && !isFirstBox) ||
               (commonState.m_currentOp != eHCLReduceScatter && commonState.m_currentOp != eHCLScatter),
           "signaling stream must be active when syncing with LTU!"
           "isActive={}, m_syncUpBufferWithLtu={}, m_currentOp={}",
           isActive,
           commonState.m_syncUpBufferWithLtu,
           commonState.m_currentOp);

    if (isActive) fillDmaStream(hcl::DMAStreams::signaling, m_archStreamIdx, schedIdx);

    // gdr
    isActive = m_scaleoutProvider->isGaudiDirect() && commonState.m_isMultiScaleupGroup &&
               currentOp == eHCLReduceScatter && !isFirstBox && !commonState.isRSContReduction();
    if (isActive) fillDmaStream(hcl::DMAStreams::gdr, m_archStreamIdx, schedIdx);

    for (unsigned i = 0; i < static_cast<size_t>(hcl::DMAStreams::max); i++)
    {
        hcl::ScalStream* scalStream = m_dmaStreams[i];
        if (scalStream) scalStream->setTargetValue(m_longSo.targetValue);
    }
}

void ActiveStreamManagerGen2Arch::fillDmaStream(hcl::DMAStreams stream, unsigned archStreamIdx, unsigned schedIdx)
{
    m_dmaStreams[static_cast<size_t>(stream)] =
        &m_deviceController.getScalStream(archStreamIdx, schedIdx, static_cast<size_t>(stream));
}

llvm_vecsmall::SmallVector<unsigned, MAX_STREAM_TO_INC> ActiveStreamManagerGen2Arch::getActiveDmaStreams() const
{
    llvm_vecsmall::SmallVector<unsigned, MAX_STREAM_TO_INC> activeStreams = {0};

    for (unsigned i = 0; i < static_cast<size_t>(hcl::DMAStreams::max); i++)
    {
        if (i == static_cast<unsigned>(hcl::DMAStreams::garbageCollection) ||
            i == static_cast<unsigned>(hcl::DMAStreams::arbitrator))
        {
            continue;
        }
        if (m_dmaStreams[i] != nullptr)
        {
            activeStreams.push_back(i);
        }
    }

    return activeStreams;
}

hcl::ScalStream& ActiveStreamManagerGen2Arch::getActiveCollectiveStream(const hcl::SchedulersIndex schedIdx)
{
    unsigned idx = 0;
    switch (m_commonState->m_currentOp)
    {
        case eHCLGather:
        case eHCLAllGather:
        case eHCLSimpleBroadcast:
        case eHCLBroadcast:
        case eHCLSinglePeerBroadcast:
            idx = static_cast<unsigned>(CollectiveStreams::AG);
            break;
        case eHCLReduceScatter:
        case eHCLScatter:
        case eHCLReduce:
        case eHCLAllReduce:
        case eHCLAll2All:
        case eHCLNoCollective:  // used in Gen2Arch for Send/Recv operations
            idx = static_cast<unsigned>(CollectiveStreams::RS);
            break;
        default:
            VERIFY(false, "collective op is not supported {}", (int)m_commonState->m_currentOp);
    }

    hcl::ScalStream& scalStream = m_deviceController.getScalStream(m_archStreamIdx, (unsigned)schedIdx, idx);
    scalStream.setTargetValue(m_longSo.targetValue);

    return scalStream;
}

hcl::ScalStream& ActiveStreamManagerGen2Arch::getArbitratorStream(const hcl::SchedulersIndex schedIdx)
{
    hcl::ScalStream& scalStream = m_deviceController.getScalStream(m_archStreamIdx, (unsigned)schedIdx, ARB_STREAM_IDX);
    scalStream.setTargetValue(m_longSo.targetValue);

    return scalStream;
}