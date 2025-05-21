#pragma once
#include "platform/gaudi_common/hcl_device_controller.h"  // for HclDeviceControllerGaudiCommon

class HclDeviceControllerGaudi2 : public HclDeviceControllerGaudiCommon
{
public:
    HclDeviceControllerGaudi2(const int fd, const unsigned numOfStreams);
    virtual ~HclDeviceControllerGaudi2()                                   = default;
    HclDeviceControllerGaudi2(const HclDeviceControllerGaudi2&)            = delete;
    HclDeviceControllerGaudi2& operator=(const HclDeviceControllerGaudi2&) = delete;

    void initGlobalContext(HclDeviceGen2Arch* device, uint8_t api_id);
    void serializeInitSequenceCommands(hcl::ScalStreamBase&                                 recvStream,
                                       hcl::ScalStreamBase&                                 recvSOStream,
                                       hcl::ScalStreamBase&                                 dmaStream,
                                       unsigned                                             indexOfCg,
                                       uint64_t                                             soAddressLSB,
                                       const std::vector<SimbPoolContainerParamsPerStream>& containerParamsPerStreamVec,
                                       HclDeviceGen2Arch*                                   device,
                                       uint8_t                                              apiId);
};