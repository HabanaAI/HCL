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
    ActiveStreamManagerGen2Arch(SliceState&                  sendSliceState,
                                ScaleoutProvider*            scaleoutProvider,
                                HclDeviceControllerGen2Arch& deviceController,
                                unsigned                     archStreamIdx,
                                unsigned                     schedIdx);

    ActiveStreamManagerGen2Arch(ActiveStreamManagerGen2Arch&&)                 = delete;
    ActiveStreamManagerGen2Arch(const ActiveStreamManagerGen2Arch&)            = delete;
    ActiveStreamManagerGen2Arch& operator=(ActiveStreamManagerGen2Arch&&)      = delete;
    ActiveStreamManagerGen2Arch& operator=(const ActiveStreamManagerGen2Arch&) = delete;
    virtual ~ActiveStreamManagerGen2Arch()                                     = default;

    llvm_vecsmall::SmallVector<unsigned, MAX_STREAM_TO_INC> getActiveDmaStreams() const;
    static hcl::ScalStream& getActiveCollectiveStream(HclDeviceControllerGen2Arch& deviceController,
                                                      HCL_CollectiveOp             currentOp,
                                                      const unsigned               archStreamIdx,
                                                      const unsigned               schedIdx);
    void                    setTargetValueForAllDmaStreams(uint64_t targetValue);

    inline hcl::ScalStream* getDmaScalStream(hcl::DMAStreams stream) { return m_dmaStreams[static_cast<int>(stream)]; }

private:
    llvm_vecsmall::SmallVector<hcl::ScalStream*, static_cast<size_t>(hcl::DMAStreams::max)> m_dmaStreams  = {};
    HclDeviceControllerGen2Arch&                                                            m_deviceController;

    void fillDmaStream(hcl::DMAStreams stream, unsigned archStreamIdx, unsigned schedIdx);
};