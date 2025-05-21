#include "platform/gen2_arch_common/hal.h"

using namespace hcl;

uint64_t Gen2ArchHal::getMaxArchStreams() const
{
    return m_maxStreams;
}

uint64_t Gen2ArchHal::getMaxQPsPerNic() const
{
    return m_maxQPsPerNic;
}

uint64_t Gen2ArchHal::getMaxNics() const
{
    return m_maxNics;
}

uint32_t Gen2ArchHal::getMaxEDMAs() const
{
    return m_maxEDMAs;
}
