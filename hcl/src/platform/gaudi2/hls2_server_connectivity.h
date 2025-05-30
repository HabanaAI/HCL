#pragma once

#include <cstdint>  // for uint*_t

#include "hcl_api_types.h"                                   // for HCL_Comm
#include "platform/gen2_arch_common/server_connectivity.h"   // for Gen2ArchServerConnectivity
#include "platform/gen2_arch_common/runtime_connectivity.h"  // for Gen2ArchRuntimeConnectivity
#include "platform/gen2_arch_common/hcl_device_config.h"     // for HclDeviceConfig
#include "platform/gaudi2/server_autogen_HLS2.h"             // for HLS2_NUM_DEVICES

class HLS2ServerConnectivity : public Gen2ArchServerConnectivity
{
public:
    HLS2ServerConnectivity(const int        fd,
                           const int        moduleId,
                           const bool       useDummyConnectivity,
                           HclDeviceConfig& deviceConfig);
    virtual ~HLS2ServerConnectivity() = default;

    unsigned getBoxSize() const override { return HLS2_NUM_DEVICES; };

protected:
    virtual Gen2ArchRuntimeConnectivity*
    createRuntimeConnectivityFactory(const int                   moduleId,
                                     const HCL_Comm              hclCommId,
                                     Gen2ArchServerConnectivity& serverConnectivity) override;

private:
};
