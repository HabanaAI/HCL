#include "platform/gaudi2/hal.h"

namespace hcl
{
uint64_t Gaudi2Hal::getFlushPCIeReg() const
{
    return m_flushReg;
}

uint32_t Gaudi2Hal::getMaxQpPerInternalNic() const
{
    return m_maxQpPerInternalNic;
}

uint32_t Gaudi2Hal::getMaxQpPerExternalNic() const
{
    return m_maxQpPerExternalNic;
}

uint32_t Gaudi2Hal::getCollectiveContextsCount() const
{
    return m_collectiveContextsCount;
}
uint64_t Gaudi2Hal::getMaxQPsPerNicNonPeer() const
{
    return m_maxQPsPerNicNonPeer;
}

}  // namespace hcl