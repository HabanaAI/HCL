#include "hcl_device_config_factory.h"

#include "platform/gaudi_common/hcl_device_config.h"  // for HclDeviceConfigGaudiCommon

#include "hcl_types.h"  // for SYN_VALID_DEVICE_ID

std::unique_ptr<HclDeviceConfig> HclDeviceConfigFactory::createDeviceConfig()
{
    return std::make_unique<HclDeviceConfigGaudiCommon>(SYN_VALID_DEVICE_ID);
}