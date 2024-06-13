#include "platform/gen2_arch_common/hal.h"

#include <algorithm>  // for generate_n
#include <set>        // for set

#include "platform/gen2_arch_common/types.h"  // for GEN2ARCH_HLS_BOX_SIZE
#include "hcl_types.h"                        // for HCL_HwModuleId

using namespace hcl;

Gen2ArchHal::Gen2ArchHal()
{
    m_hwModuleIds.clear();
    HCL_HwModuleId n(0);
    std::generate_n(std::inserter(m_hwModuleIds, m_hwModuleIds.begin()), GEN2ARCH_HLS_BOX_SIZE, [n]() mutable {
        return n++;
    });
}

uint64_t Gen2ArchHal::getMaxStreams() const
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

uint32_t Gen2ArchHal::getDefaultBoxSize() const
{
    return m_defaultBoxSize;
}

uint32_t Gen2ArchHal::getDefaultPodSize() const
{
    return m_defaultPodSize;
}

const std::set<HCL_HwModuleId>& Gen2ArchHal::getHwModules() const
{
    return m_hwModuleIds;
}
