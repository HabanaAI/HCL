#pragma once

#include <cstdint>  // for uint*_t

#include "hcl_api_types.h"                                   // for HCL_Comm
#include "platform/gen2_arch_common/runtime_connectivity.h"  // for Gen2ArchRuntimeConnectivity
#include "platform/gaudi2/server_autogen_HLS2.h"             // for HLS2_NUM_SCALEUP_NICS_PER_DEVICE

class HLS2RuntimeConnectivity : public Gen2ArchRuntimeConnectivity
{
public:
    HLS2RuntimeConnectivity(const int                   moduleId,
                            const HCL_Comm              hclCommId,
                            Gen2ArchServerConnectivity& serverConnectivity);
    virtual ~HLS2RuntimeConnectivity() = default;

    virtual uint32_t getBackpressureOffset(const uint16_t nic) const override;

    // Needs to be adjusted per comm
    virtual uint16_t getMaxNumScaleUpPortsPerConnection() const override { return HLS2_NUM_SCALEUP_NICS_PER_DEVICE; }

protected:
    virtual void initServerSpecifics() override;
};
