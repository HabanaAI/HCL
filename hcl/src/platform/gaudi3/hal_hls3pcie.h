#pragma once

#include <set>  // for set

#include "platform/gaudi3/hal.h"             // for Gaudi3Hal
#include "platform/gaudi3/types_hls3pcie.h"  // for HLS3PCIE_BOX_SIZE, HLS3PCIE_NUM_SCALEUP_PORTS_PER_CONNECTION
#include "hcl_types.h"                       // for HCL_HwModuleId

namespace hcl
{
class Gaudi3Hls3PCieHal : public Gaudi3Hal
{
public:
    Gaudi3Hls3PCieHal(const uint32_t hwModuleId);
    virtual ~Gaudi3Hls3PCieHal() = default;

    virtual uint32_t                        getDefaultBoxSize() const override { return m_defaultBoxSize; }
    virtual uint32_t                        getDefaultPodSize() const override { return m_defaultPodSize; }
    virtual const std::set<HCL_HwModuleId>& getHwModules() const override;
    virtual unsigned                        getMaxNumScaleUpPortsPerConnection() const override
    {
        return HLS3PCIE_NUM_SCALEUP_PORTS_PER_CONNECTION;
    }

private:
    const uint32_t m_defaultBoxSize = HLS3PCIE_BOX_SIZE;  // Amount of Gaudis with any to any connectivity in each box
    const uint32_t m_defaultPodSize =
        HLS3PCIE_BOX_SIZE;  // Amount of Gaudis with any to any connectivity in each box for default communicator
    const HCL_HwModuleId m_hwModuleId;
};

}  // namespace hcl
