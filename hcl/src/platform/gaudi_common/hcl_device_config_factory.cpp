#include "hcl_device_config_factory.h"

#include "platform/gaudi_common/hcl_device_config.h"  // for HclDeviceConfigGaudiCommon
#include "platform/gaudi_common/gaudi_consts.h"       // for SYN_VALID_DEVICE_ID

#include "hcl_utils.h"  // for VERIFY

std::unique_ptr<HclDeviceConfig> HclDeviceConfigFactory::createDeviceConfig(void* device, void* context)
{
    VERIFY(device == nullptr, "device is expected to be nullptr");
    VERIFY(context == nullptr, "context is expected to be nullptr");

    return std::make_unique<HclDeviceConfigGaudiCommon>(SYN_VALID_DEVICE_ID);
}