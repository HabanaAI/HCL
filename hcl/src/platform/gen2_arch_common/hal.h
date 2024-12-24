#pragma once

#include "interfaces/hcl_hal.h"

#include <cstdint>  // for uint32_t, uint64_t

#include "platform/gen2_arch_common/types.h"  // for MAX_NICS_GEN2ARCH

namespace hcl
{
class Gen2ArchHal : public Hal
{
public:
    Gen2ArchHal()                              = default;
    virtual ~Gen2ArchHal()                     = default;
    Gen2ArchHal(const Gen2ArchHal&)            = delete;
    Gen2ArchHal& operator=(const Gen2ArchHal&) = delete;

    virtual uint64_t getMaxStreams() const override;
    virtual uint64_t getMaxQPsPerNic() const override;
    virtual uint64_t getMaxNics() const override;
    virtual uint32_t getMaxEDMAs() const override;

    virtual uint32_t getMaxQpPerInternalNic() const override = 0;
    virtual uint32_t getMaxQpPerExternalNic() const override = 0;

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
    const uint64_t m_maxNics      = MAX_NICS_GEN2ARCH;
    const uint64_t m_maxEDMAs     = 2;
};

}  // namespace hcl
