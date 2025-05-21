#pragma once
#include "platform/gen2_arch_common/device_simb_pool_manager.h"

class DeviceSimbPoolManagerGaudi2 : public DeviceSimbPoolManagerBase
{
public:
    DeviceSimbPoolManagerGaudi2(std::array<SimbPoolContainerParamsPerStream, MAX_POOL_CONTAINER_IDX> spcParamsPerStream,
                                const std::map<e_devicePoolID, unsigned>&                            sizes);
    ~DeviceSimbPoolManagerGaudi2() override = default;

    virtual unsigned getPoolContainerIndex(const e_devicePoolID poolIdx) const override;

private:
    // Add any member variables specific to Gaudi2 if needed
};
