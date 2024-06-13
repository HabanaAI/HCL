#pragma once

#include "interfaces/hcl_hal.h"

#include <cstdint>              // for uint32_t, uint64_t
#include <set>                  // for set

#include "platform/gen2_arch_common/types.h"  // for GEN2ARCH_HLS_BOX_SIZE
#include "hcl_types.h"  // for DEFAULT_COMMUNICATORS_SIZE, HCL_HwModuleId, NUM_SCALEUP_PORTS_PER_CONNECTION

namespace hcl
{
class Gen2ArchHal : public Hal
{
public:
    Gen2ArchHal();

    virtual uint64_t getMaxStreams() const override;
    virtual uint64_t getMaxQPsPerNic() const override;
    virtual uint64_t getMaxNics() const override;
    virtual uint32_t getMaxEDMAs() const override;

    virtual uint32_t getDefaultBoxSize() const override;
    virtual uint32_t getDefaultPodSize() const override;

    virtual uint64_t getFlushPCIeReg() const override = 0;

    virtual uint32_t getMaxQpPerInternalNic() const override = 0;
    virtual uint32_t getMaxQpPerExternalNic() const override = 0;

    virtual const std::set<HCL_HwModuleId>& getHwModules() const override;
    virtual unsigned getMaxNumScaleUpPortsPerConnection() const override { return NUM_SCALEUP_PORTS_PER_CONNECTION; }

protected:
    // multi streams
    const uint32_t m_queueOffsetTags = 0;
    const uint32_t m_queueOffset1    = 1;
    const uint32_t m_queueOffset2    = 2;
    const uint32_t m_queueOffset3    = 3;
    const uint32_t m_queueOffsetMax  = 4;

    // streams definitions
    const uint64_t m_maxStreams   = 3;
    const uint64_t m_maxQPsPerNic = 4;
    const uint64_t m_maxNics      = 24;
    const uint64_t m_maxEDMAs     = 2;

    std::set<HCL_HwModuleId> m_hwModuleIds;  // module ids inside the box with me

private:
    const uint32_t m_defaultBoxSize = GEN2ARCH_HLS_BOX_SIZE;
    const uint32_t m_defaultPodSize = GEN2ARCH_HLS_BOX_SIZE;  // Amount of Gaudis with any to any connectivity
    const uint32_t m_maxCommGroups  = DEFAULT_COMMUNICATORS_SIZE;
};

}  // namespace hcl
