#pragma once

#include "hcl_api_types.h"
#include "collective_states.h"
#include "platform/gen2_arch_common/hcl_device_controller.h"

namespace hcl
{
class ScalStream;
}

class ScaleoutProvider;

/**
 * @brief Manages active streams for Gen2 architecture.
 *
 * This class is responsible for initializing and managing active streams
 * in the Gen2 architecture. It provides methods to get active collective
 * streams, arbitrator streams, and GP streams if needed. It also allows
 * adding streams to the active stream list and checking the status of
 * various types of streams.
 */
class ActiveStreamManagerGen2Arch
{
public:
    ActiveStreamManagerGen2Arch(ScaleoutProvider*            scaleoutProvider,
                                HclDeviceControllerGen2Arch& deviceController,
                                unsigned                     archStreamIdx,
                                hcl::syncInfo&               longSo);

    ActiveStreamManagerGen2Arch(ActiveStreamManagerGen2Arch&&)                 = delete;
    ActiveStreamManagerGen2Arch(const ActiveStreamManagerGen2Arch&)            = delete;
    ActiveStreamManagerGen2Arch& operator=(ActiveStreamManagerGen2Arch&&)      = delete;
    ActiveStreamManagerGen2Arch& operator=(const ActiveStreamManagerGen2Arch&) = delete;
    virtual ~ActiveStreamManagerGen2Arch()                                     = default;

    /**
     * @brief Initializes the active streams for a given iteration.
     */
    void initializeActiveStreams(CommonState& commonState, unsigned boxNum);

    /**
     * @brief Retrieves the active collective stream for the given scheduler index - AG/RS
     */
    hcl::ScalStream& getActiveCollectiveStream(hcl::SchedulersIndex schedIdx);

    /**
     * @brief Gets the arbitrator stream for a given scheduler and increments its targetValue for ccb management
     */
    hcl::ScalStream& getArbitratorStream(const hcl::SchedulersIndex schedIdx);

    /**
     * @brief Retrieves the active streams for a given scheduler index.
     * is is used to let the arbitrator know what streams to free once we have the required credits
     *
     * @param schedIdx The index of the scheduler for which to retrieve active streams.
     * @return A SmallVector containing the active streams for the given scheduler index.
     */
    llvm_vecsmall::SmallVector<unsigned, MAX_STREAM_PER_SCHED> getActiveStreams(hcl::SchedulersIndex schedIdx) const;

    hcl::ScalStream* getScalStreamIfNeeded(HclStreamIndex hclStreamIdx);

    void addStreamToActiveStreamList(HclStreamIndex hclStreamIdx);

protected:
    void                         addCollectiveStreamToActiveStreamList(hcl::SchedulersIndex schedIdx);
    bool                         isReductionStreamActive(bool reductionRS);
    bool                         isScaleoutReductionStreamActive(bool isLastBox);
    bool                         isSignalingStreamActive(bool isReductionStreamActive, bool isFirstBox);
    bool                         isGdrStreamActive(bool isFirstBox);
    HclDeviceControllerGen2Arch& m_deviceController;
    ScaleoutProvider*            m_scaleoutProvider;
    unsigned                     m_archStreamIdx;
    hcl::syncInfo&               m_longSo;
    CommonState*                 m_commonState;

    llvm_vecsmall::SmallVector<hcl::ScalStream*, 0> m_activeStreamsPerScheduler[5];
};