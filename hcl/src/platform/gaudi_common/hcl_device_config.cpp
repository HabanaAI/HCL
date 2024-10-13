
#include "platform/gaudi_common/hcl_device_config.h"

#include "hcl_utils.h"             // for LOG_*
#include "hlthunk.h"               // for hlthunk_get_hw_ip_info
#include "drm/habanalabs_accel.h"  // for hl_server_type, HL_SERVER_GA...
#include "synapse_api_types.h"     // for synDeviceId
#include "synapse_api.h"           // for synDeviceGetInfoV2

HclDeviceConfigGaudiCommon::HclDeviceConfigGaudiCommon(const synDeviceId deviceId)
: HclDeviceConfig(), m_deviceId(deviceId)
{
    if (deviceId == SYN_VALID_DEVICE_ID)  // Real device, not unit test
    {
        synDeviceInfoV2 deviceInfo = {};

        VERIFY(synSuccess == synDeviceGetInfoV2(SYN_VALID_DEVICE_ID, &deviceInfo));
        m_fd         = deviceInfo.fd;
        m_deviceType = deviceInfo.deviceType;

        int rc = hlthunk_get_pci_bus_id_from_fd(m_fd, m_pciBusId, sizeof(m_pciBusId));
        VERIFY(rc == 0, "hlthunk_get_pci_bus_id_from_fd() failed: {}", rc);

        /* Get device index from bus ID */
        m_deviceIndex = hlthunk_get_device_index_from_pci_bus_id(m_pciBusId);

        readHwType();
        const std::string accel = getHLDevice(m_fd);
        LOG_HCL_INFO(HCL, "This rank is using device: {} OAM: {}", accel, m_hwModuleID);
    }
}

bool HclDeviceConfigGaudiCommon::isDeviceAcquired() const
{
    return (getSynDeviceId() == SYN_VALID_DEVICE_ID);
}

void HclDeviceConfigGaudiCommon::readHwType()
{
    LOG_HCL_DEBUG(HCL, "Started");
    struct hlthunk_hw_ip_info hw_ip;

    const int rc = hlthunk_get_hw_ip_info(m_fd, &hw_ip);
    if (!rc)
    {
        m_ServerType = (hl_server_type)hw_ip.server_type;
        LOG_HCL_INFO(HCL, "Received server type from driver: {} ({})", m_ServerType, (int)m_ServerType);
        m_hwModuleID = hw_ip.module_id;
        LOG_HCL_INFO(HCL, "Received module ID from driver: {}", m_hwModuleID);
        m_sramBaseAddress = hw_ip.sram_base_address;
        LOG_HCL_DEBUG(HCL, "m_sramBaseAddress=(0x{:x})", m_sramBaseAddress);
        m_dramEnabled = hw_ip.dram_enabled;
        LOG_HCL_DEBUG(HCL, "m_dramEnabled={}", m_dramEnabled);
    }
    else
    {
        LOG_HCL_CRITICAL(HCL, "Failed to read hlthunk hw info, rc={}", rc);
        VERIFY(0 == rc, "Failed to read hlthunk hw info, rc={}", rc);
    }
}

bool HclDeviceConfigGaudiCommon::determineHclType()
{
    const hl_server_type server_type = m_ServerType;
    LOG_HCL_INFO(HCL, "Received server type from driver: {} ({})", server_type, (int)server_type);

    if (GCFG_BOX_TYPE.isSetFromUserConfig())
    {
        LOG_HCL_INFO(HCL, "Server type is set by user to {}, ignoring driver type", GCFG_BOX_TYPE.value());
        return validateHclType();
    }

    HclConfigType configTypeFromServer;
    switch (server_type)
    {
        case HL_SERVER_TYPE_UNKNOWN:
            configTypeFromServer = BACK_2_BACK;
            break;
        case HL_SERVER_GAUDI_HLS1:
            configTypeFromServer = HLS1;
            break;
        case HL_SERVER_GAUDI_HLS1H:
            configTypeFromServer = HLS1H;
            break;
        case HL_SERVER_GAUDI_TYPE1:
        case HL_SERVER_GAUDI_TYPE2:
            configTypeFromServer = OCP1;
            break;
        case HL_SERVER_GAUDI2_TYPE1:  // FALLTHROUGH
        case HL_SERVER_GAUDI2_HLS2:
            configTypeFromServer = HLS2;
            break;
        case HL_SERVER_GAUDI3_HLS3_FULL_OAM_3PORTS_SCALE_OUT:
            configTypeFromServer = HLS3;
            break;
        case HL_SERVER_GAUDI3_HL338:
            configTypeFromServer = HL338;
            break;
        default:
            LOG_HCL_CRITICAL(HCL, "Got unknown server_type ({}) from driver", server_type);
            configTypeFromServer = UNKNOWN;
            break;
    }

    GCFG_BOX_TYPE.setValue(g_boxTypeIdToStr.at(configTypeFromServer));
    GCFG_BOX_TYPE_ID.setValue(configTypeFromServer);

    return validateHclType();
}

bool HclDeviceConfigGaudiCommon::validateHclType()
{
    if (m_fd == -1) return true; /* No device tests */

    /* No default in switch case to enforce adding new enums */
    HclConfigType configType = (HclConfigType)GCFG_BOX_TYPE_ID.value();

    switch (configType)
    {
        case HLS1:
        case HLS1H:
        case OCP1:
        case UNKNOWN:
            LOG_HCL_CRITICAL(HCL, "Invalid HCL_TYPE value ({})", configType);
            return false;
        case HLS2:
            if (!IS_DEVICE_GAUDI2(m_deviceType))
            {
                LOG_HCL_CRITICAL(HCL, "Invalid HCL_TYPE value ({}) for Gaudi2", configType);
                return false;
            }
            break;
        case HLS3:
        case HL338:
            if (!IS_DEVICE_GAUDI3(m_deviceType))
            {
                LOG_HCL_CRITICAL(HCL, "Invalid HCL_TYPE value ({}) for Gaudi3", configType);
                return false;
            }
            break;
        case BACK_2_BACK:
        case RING:
        case LOOPBACK:
            break;
    }

    return true;
}
