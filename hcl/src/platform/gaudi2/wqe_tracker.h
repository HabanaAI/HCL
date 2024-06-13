#pragma once
#include "hcl_api_types.h"
#include "hcl_dynamic_communicator.h"
#include "platform/gen2_arch_common/wqe_tracker.h"

class WqeTrackerGaudi2 : public WqeTracker
{
public:
    explicit WqeTrackerGaudi2();
    virtual ~WqeTrackerGaudi2() = default;

    void              incWqe(const HCL_Comm commId, const unsigned rank, const QpType qpType) override;
    WqeWraparoundBits getWqeWraparoundBits(HCL_Comm commId, unsigned rank, QpType qpType) override;

private:
    WqePerConnection       m_wqePerConnection;
    WqeWraparoundBitsPerQp m_wqeWraparoundBits;
};
