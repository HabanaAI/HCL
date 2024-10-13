#pragma once

#include <array>    // for array
#include <cstdint>  // for uint8_t
#include <map>      // for map
#include <tuple>    // for tuple

#include "hcl_api_types.h"                                   // for HCL_Comm
#include "platform/gen2_arch_common/runtime_connectivity.h"  // for Gen2ArchRuntimeConnectivity
#include "platform/gaudi2/server_autogen_HLS2PCIE.h"         // for HLS2PCIE_NUM_SCALEUP_NICS_PER_DEVICE

class HLS2PCIERuntimeConnectivity : public Gen2ArchRuntimeConnectivity
{
public:
    HLS2PCIERuntimeConnectivity(const int                   moduleId,
                                const HCL_Comm              hclCommId,
                                Gen2ArchServerConnectivity& serverConnectivity);
    virtual ~HLS2PCIERuntimeConnectivity() = default;

    virtual uint32_t getBackpressureOffset(const uint16_t nic) const override;

    // Needs to be adjusted per comm
    virtual uint16_t getMaxNumScaleUpPortsPerConnection() const override
    {
        return HLS2PCIE_NUM_SCALEUP_NICS_PER_DEVICE;
    }

protected:
    virtual void initServerSpecifics() override;
};
