#include "platform/gaudi3/hal.h"

#include "interfaces/hcl_hal.h"  // for NOT_IMPLEMENTED

namespace hcl
{
uint32_t Gaudi3Hal::getMaxQpPerInternalNic() const
{
    return m_maxQpPerInternalNic;
}

uint32_t Gaudi3Hal::getMaxQpPerExternalNic() const
{
    return m_maxQpPerExternalNic;
}

uint64_t Gaudi3Hal::getMaxQPsPerNic() const
{
    return m_maxQPsPerNic;
}

uint32_t Gaudi3Hal::getMaxEDMAs() const
{
    return m_maxEDMAs;
}

}  // namespace hcl