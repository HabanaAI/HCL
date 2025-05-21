#pragma once
#include "platform/gen2_arch_common/device_simb_pool_manager.h"

class DeviceSimbPoolManagerGaudi3 : public DeviceSimbPoolManagerBase
{
public:
    DeviceSimbPoolManagerGaudi3(std::array<SimbPoolContainerParamsPerStream, MAX_POOL_CONTAINER_IDX> spcParamsPerStream,
                                const std::map<e_devicePoolID, unsigned>&                            sizes);
    ~DeviceSimbPoolManagerGaudi3() override = default;

    virtual unsigned getPoolContainerIndex(const e_devicePoolID poolIdx) const override;

private:
};
