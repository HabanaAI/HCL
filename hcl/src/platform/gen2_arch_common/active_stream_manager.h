#pragma once

#include "hcl_api_types.h"
#include "collective_states.h"
#include "platform/gen2_arch_common/hcl_device_controller.h"

namespace hcl
{
class ScalStream;
}

class ScaleoutProvider;

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

    void initializeDmaStreams(CommonState& commonState, unsigned boxNum);

    llvm_vecsmall::SmallVector<unsigned, MAX_STREAM_TO_INC> getActiveDmaStreams() const;
    hcl::ScalStream& getActiveCollectiveStream(const hcl::SchedulersIndex schedIdx);
    hcl::ScalStream& getArbitratorStream(const hcl::SchedulersIndex schedIdx);

    inline hcl::ScalStream* getDmaScalStream(hcl::DMAStreams stream) { return m_dmaStreams[static_cast<int>(stream)]; }
    void                    fillDmaStream(hcl::DMAStreams stream, unsigned archStreamIdx, unsigned schedIdx);

private:
    llvm_vecsmall::SmallVector<hcl::ScalStream*, static_cast<size_t>(hcl::DMAStreams::max)> m_dmaStreams = {};

    HclDeviceControllerGen2Arch& m_deviceController;
    ScaleoutProvider*            m_scaleoutProvider;
    unsigned                     m_archStreamIdx;
    hcl::syncInfo&               m_longSo;

    CommonState* m_commonState;
};