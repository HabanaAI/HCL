#pragma once

#include "hcl_api_types.h"                                     // for HCL_Comm
#include "platform/gaudi3/gaudi3_base_runtime_connectivity.h"  // for Gaudi3BaseRuntimeConnectivity
#include "platform/gaudi3/gaudi3_base_server_connectivity.h"   // for Gaudi3BaseServerConnectivity
#include "platform/gaudi3/server_autogen_HLS3Rack.h"           // for HLS3RACK_NUM_SCALEUP_NICS_PER_DEVICE

//
// Configuration per comm
//
class HLS3RackRuntimeConnectivity : public Gaudi3BaseRuntimeConnectivity
{
public:
    HLS3RackRuntimeConnectivity(const int                   moduleId,
                                const HCL_Comm              hclCommId,
                                Gen2ArchServerConnectivity& serverConnectivity);
    virtual ~HLS3RackRuntimeConnectivity() = default;

    // Needs to be adjusted per comm
    virtual uint16_t getMaxNumScaleUpPortsPerConnection() const override
    {
        return HLS3RACK_NUM_SCALEUP_NICS_PER_DEVICE;
    }

    virtual uint32_t getBackpressureOffset(const uint16_t nic) const override;
};
