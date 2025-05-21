#pragma once

#include <cstdint>  // for uint8_t

#include "hcl_api_types.h"                                    // for HCL_Comm
#include "platform/gen2_arch_common/server_connectivity.h"    // for Gen2ArchServerConnectivity
#include "platform/gen2_arch_common/runtime_connectivity.h"   // for Gen2ArchRuntimeConnectivity
#include "platform/gaudi3/gaudi3_base_server_connectivity.h"  // for Gaudi3BaseServerConnectivity

class HLS3RackServerConnectivity : public Gaudi3BaseServerConnectivity
{
public:
    HLS3RackServerConnectivity(const int        fd,
                               const int        moduleId,
                               const bool       useDummyConnectivity,
                               HclDeviceConfig& deviceConfig);
    virtual ~HLS3RackServerConnectivity() = default;

protected:
    virtual Gen2ArchRuntimeConnectivity*
    createRuntimeConnectivityFactory(const int                   moduleId,
                                     const HCL_Comm              hclCommId,
                                     Gen2ArchServerConnectivity& serverConnectivity) override;

private:
};
