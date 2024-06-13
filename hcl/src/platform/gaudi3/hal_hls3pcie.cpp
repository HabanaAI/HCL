#include "platform/gaudi3/hal_hls3pcie.h"

#include <algorithm>  // for generate_n
#include <set>        // for set

#include "hcl_types.h"  // for HCL_HwModuleId

namespace hcl
{

Gaudi3Hls3PCieHal::Gaudi3Hls3PCieHal(const uint32_t hwModuleId) : Gaudi3Hal(), m_hwModuleId(hwModuleId)
{
    m_hwModuleIds.clear();
    HCL_HwModuleId n((hwModuleId >= HLS3PCIE_BOX_SIZE) ? HLS3PCIE_BOX_SIZE : 0);
    std::generate_n(std::inserter(m_hwModuleIds, m_hwModuleIds.begin()), HLS3PCIE_BOX_SIZE, [n]() mutable {
        return n++;
    });
}

const std::set<HCL_HwModuleId>& Gaudi3Hls3PCieHal::getHwModules() const
{
    return m_hwModuleIds;
}

}  // namespace hcl