
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

ActiveStreamManagerGen2Arch::ActiveStreamManagerGen2Arch(SliceState&                  sendSliceState,
                                                         ScaleoutProvider*            scaleoutProvider,
                                                         HclDeviceControllerGen2Arch& deviceController,
                                                         unsigned                     archStreamIdx,
                                                         unsigned                     schedIdx)
: m_deviceController(deviceController)
{
    BoxNumInfo&      sendBoxNumInfo = sendSliceState.m_boxNumInfo;
    HCL_CollectiveOp currentOp = sendSliceState.m_currentOp;
    bool             isActive  = false;

    bool isHierarchicalSelfBox =
        (sendBoxNumInfo.m_boxNum == sendSliceState.m_dynamicComm.getMyPod() && sendSliceState.m_isMultiPod);
    bool reductionRS           = (currentOp == eHCLReduceScatter) && (!isHierarchicalSelfBox);
    bool reductionReduce       = false;
    bool isLastBox             = (getNextBox(sendBoxNumInfo.m_boxNum, sendSliceState.m_boxIterations) ==
                      sendSliceState.m_dynamicComm.getMyPod());
    bool isFirstBox            = sendBoxNumInfo.m_boxNum == sendSliceState.m_dynamicComm.getMyPod();

    m_dmaStreams.resize(static_cast<size_t>(hcl::DMAStreams::max));
    std::fill(m_dmaStreams.begin(), m_dmaStreams.end(), nullptr);

    // arbitrator
    fillDmaStream(hcl::DMAStreams::arbitrator, archStreamIdx, schedIdx);

    // garbageCollection
    fillDmaStream(hcl::DMAStreams::garbageCollection, archStreamIdx, schedIdx);

    // reduction
    isActive = ((sendSliceState.m_16BitReduction || (!sendSliceState.m_inPlace && !sendSliceState.m_isMultiPod)) &&
                reductionRS && sendSliceState.m_dynamicComm.getPodSize() != 1) ||
               reductionReduce || (currentOp == eHCLReduceScatter && sendSliceState.m_dynamicComm.getPodSize() != 1);
    bool isReductionStreamActive = isActive;
    if (isActive) fillDmaStream(hcl::DMAStreams::reduction, archStreamIdx, schedIdx);

    // scaleoutReduction
    isActive = sendSliceState.m_isMultiPod && isLastBox && currentOp == eHCLReduceScatter;
    if (isActive) fillDmaStream(hcl::DMAStreams::scaleoutReduction, archStreamIdx, schedIdx);

    // signaling
    isActive = false;
    if (currentOp == eHCLReduceScatter)
    {
        bool incLtu     = sendSliceState.m_syncUpBufferWithLtu;
        bool isPdmaHnic = scaleoutProvider->isHostNic() && !scaleoutProvider->isGaudiDirect();
        isActive        = sendSliceState.m_isMultiPod &&
                   ((((isFirstBox && !GCFG_HCL_USE_EDMA_COMMAND_V3.value()) || (!isFirstBox && incLtu)) &&
                     isReductionStreamActive) ||
                    (!isFirstBox && isPdmaHnic));
    }
    else if (currentOp == eHCLScatter)
    {
        bool isRootBox  = sendSliceState.m_dynamicComm.getMyPod() == sendSliceState.rootBox();
        bool isPdmaHnic = scaleoutProvider->isHostNic() && !scaleoutProvider->isGaudiDirect();
        isActive        = sendSliceState.m_isMultiPod && (!isFirstBox && isPdmaHnic && !isRootBox);
    }

    VERIFY(isActive || !(sendSliceState.m_syncUpBufferWithLtu && !isFirstBox) ||
               (sendSliceState.m_currentOp != eHCLReduceScatter && sendSliceState.m_currentOp != eHCLScatter),
           "signaling stream must be active when syncing with LTU!"
           "isActive={}, m_syncUpBufferWithLtu={}, m_currentOp={}",
           isActive,
           sendSliceState.m_syncUpBufferWithLtu,
           sendSliceState.m_currentOp);

    if (isActive) fillDmaStream(hcl::DMAStreams::signaling, archStreamIdx, schedIdx);

    // gdr
    isActive = scaleoutProvider->isGaudiDirect() && sendSliceState.m_isMultiPod && currentOp == eHCLReduceScatter &&
               !isFirstBox;
    if (isActive) fillDmaStream(hcl::DMAStreams::gdr, archStreamIdx, schedIdx);
}

void ActiveStreamManagerGen2Arch::fillDmaStream(hcl::DMAStreams stream, unsigned archStreamIdx, unsigned schedIdx)
{
    m_dmaStreams[static_cast<size_t>(stream)] =
        &m_deviceController.getScalStream(archStreamIdx, schedIdx, static_cast<size_t>(stream));
}

void ActiveStreamManagerGen2Arch::setTargetValueForAllDmaStreams(uint64_t targetValue)
{
    for (unsigned i = 0; i < static_cast<size_t>(hcl::DMAStreams::max); i++)
    {
        hcl::ScalStream* scalStream = m_dmaStreams[i];
        if (scalStream) scalStream->setTargetValue(targetValue);
    }
}

llvm_vecsmall::SmallVector<unsigned, MAX_STREAM_TO_INC> ActiveStreamManagerGen2Arch::getActiveDmaStreams() const
{
    llvm_vecsmall::SmallVector<unsigned, MAX_STREAM_TO_INC> activeStreams = {0};

    for (unsigned i = 0; i < static_cast<size_t>(hcl::DMAStreams::max); i++)
    {
        if (i == static_cast<size_t>(hcl::DMAStreams::garbageCollection) ||
            i == static_cast<size_t>(hcl::DMAStreams::arbitrator))
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

hcl::ScalStream& ActiveStreamManagerGen2Arch::getActiveCollectiveStream(HclDeviceControllerGen2Arch& deviceController,
                                                                        HCL_CollectiveOp             currentOp,
                                                                        unsigned                     archStreamIdx,
                                                                        unsigned                     schedIdx)
{
    unsigned idx = 0;
    switch (currentOp)
    {
        case eHCLReduceScatter:
        case eHCLScatter:
            idx = static_cast<unsigned int>(CollectiveStreams::RS);
            break;
        case eHCLGather:
        case eHCLAllGather:
        case eHCLSimpleBroadcast:
        case eHCLBroadcast:
        case eHCLSinglePeerBroadcast:
            idx = static_cast<unsigned int>(CollectiveStreams::AG);
            break;
        case eHCLReduce:
        case eHCLAllReduce:
            idx = currentOp == eHCLReduceScatter ? static_cast<unsigned int>(CollectiveStreams::RS)
                                                 : static_cast<unsigned int>(CollectiveStreams::AG);
            break;
        case eHCLAll2All:
        case eHCLNoCollective:  // used in Gen2Arch for Send/Recv operations
            idx = static_cast<unsigned int>(CollectiveStreams::RS);
            break;
        default:
            VERIFY(false, "collective op is not supported {}", (int)currentOp);
    }

    return deviceController.getScalStream(archStreamIdx, schedIdx, idx);
}