#pragma once

#include <set>  // for set

#include "platform/gaudi3/hal.h"  // for Gaudi3Hal
#include "hcl_types.h"            // for HCL_HwModuleId

static constexpr unsigned HLS3PCIE_BOX_SIZE = 4;

namespace hcl
{
class Gaudi3Hls3PCieHal : public Gaudi3Hal
{
public:
    Gaudi3Hls3PCieHal(const uint32_t hwModuleId);
    virtual ~Gaudi3Hls3PCieHal()                           = default;
    Gaudi3Hls3PCieHal(const Gaudi3Hls3PCieHal&)            = delete;
    Gaudi3Hls3PCieHal& operator=(const Gaudi3Hls3PCieHal&) = delete;

    virtual uint32_t          getDefaultBoxSize() const override { return m_defaultBoxSize; }
    virtual uint32_t          getDefaultScaleupGroupSize() const override { return m_defaultScaleupGroupSize; }
    virtual const DevicesSet& getHwModules() const override;

private:
    const uint32_t m_defaultBoxSize = HLS3PCIE_BOX_SIZE;  // Amount of Gaudis with any to any connectivity in each box
    const uint32_t m_defaultScaleupGroupSize =
        HLS3PCIE_BOX_SIZE;  // Amount of Gaudis with any to any connectivity in each box for default communicator
    const HCL_HwModuleId m_hwModuleId;
};

}  // namespace hcl
