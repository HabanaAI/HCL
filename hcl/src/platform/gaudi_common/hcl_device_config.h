#pragma once

#include "platform/gen2_arch_common/hcl_device_config.h"
#include "synapse_api_types.h"     // for synDeviceId
#include "drm/habanalabs_accel.h"  // for hl_server_type
#include "synapse_common_types.h"  // for synDeviceType

static const std::map<synDeviceType, std::string> s_synDeviceTypeToStr = {{synDeviceGaudi, "synDeviceGaudi"},
                                                                          {synDeviceGaudi2, "synDeviceGaudi2"},
                                                                          {synDeviceGaudi3, "synDeviceGaudi3"},
                                                                          {synDeviceEmulator, "synDeviceEmulator"}};

class HclDeviceConfigGaudiCommon : public HclDeviceConfig
{
public:
    HclDeviceConfigGaudiCommon() = default;                  // unit tests ctor
    HclDeviceConfigGaudiCommon(const synDeviceId deviceId);  // runtime ctor
    HclDeviceConfigGaudiCommon(const HclDeviceConfigGaudiCommon&)            = delete;
    HclDeviceConfigGaudiCommon& operator=(const HclDeviceConfigGaudiCommon&) = delete;

    virtual const std::string getDeviceTypeStr() const override { return s_synDeviceTypeToStr.at(m_deviceType); }
    synDeviceType             getDeviceType() const { return m_deviceType; }
    void setDeviceType(const synDeviceType deviceType) { m_deviceType = deviceType; }  // for unit tests init only
    hl_server_type getServerType() const { return m_ServerType; }
    synDeviceId    getSynDeviceId() const { return m_deviceId; }
    virtual bool   isDeviceAcquired() const override;

private:
    virtual void readHwType() override;
    virtual bool determineHclType() override;
    virtual bool validateHclType() override;

    synDeviceType  m_deviceType = synDeviceTypeInvalid;
    hl_server_type m_ServerType = HL_SERVER_TYPE_UNKNOWN;
    synDeviceId    m_deviceId   = SYN_INVALID_DEVICE_ID;

};  // class HclDeviceConfigGaudiCommon