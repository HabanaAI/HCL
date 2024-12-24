#pragma once

#include "platform/gen2_arch_common/hal.h"

#include "hcl_global_conf.h"  // for GCFG...

namespace hcl
{
class Gaudi3Hal : public Gen2ArchHal
{
public:
    Gaudi3Hal()                            = default;
    virtual ~Gaudi3Hal()                   = default;
    Gaudi3Hal(const Gaudi3Hal&)            = delete;
    Gaudi3Hal& operator=(const Gaudi3Hal&) = delete;

    virtual uint32_t getMaxQpPerInternalNic() const override;
    virtual uint32_t getMaxQpPerExternalNic() const override;
    virtual uint64_t getMaxQPsPerNic() const override;
    virtual uint32_t getMaxEDMAs() const override;

private:
    const uint32_t m_maxQpPerInternalNic = 100;
    const uint32_t m_maxQpPerExternalNic = GCFG_MAX_QP_PER_EXTERNAL_NIC.value();
    const uint64_t m_maxQPsPerNic        = 6;
    const uint32_t m_maxEDMAs            = 4;
};

}  // namespace hcl
