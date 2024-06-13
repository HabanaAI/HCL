#include "hcl_types.h"

#include <cstdint>          // for uint64_t
#include <list>             // for list
#include <string>           //
#include <ostream>          // for opera...
#include <array>            // for array
#include "hcl_api_types.h"  // for HCL_Rank
#include "hcl_utils.h"      // for VERIFY, LOG_H...
#include <set>              // for set

std::ostream& operator<<(std::ostream& os, const HCL_CollectiveOp& hclCollectiveOp)
{
    static constexpr size_t maxEnum               = static_cast<size_t>(HCL_CollectiveOp::eHCLCollectiveLastValue);
    VERIFY( (size_t) hclCollectiveOp < maxEnum);
    static const std::array HCL_COLLECTIVE_OP_STR = {"Reduce",
                                                     "AllReduce",
                                                     "ReduceScatter",
                                                     "All2All",
                                                     "Broadcast",
                                                     "AllGather",
                                                     "Gather",
                                                     "Scatter",
                                                     "SimpleBroadcast",
                                                     "NoCollective",
                                                     "eHCLSinglePeerBroadcast"};

    static_assert(HCL_COLLECTIVE_OP_STR.size() == maxEnum);

    return os << HCL_COLLECTIVE_OP_STR[hclCollectiveOp];
}

/**
 * @brief access QP data of a nic, locate its index
 */
GaudiNicQPs::NicQPs& GaudiNicQPs::operator[](uint8_t nic)
{
    // search for nic nic_ap_data entry
    for (size_t i = 0; i < COMPACT_RANK_INFO_NICS; i++)
    {
        if (this->qp[i].nic == nic)
        {
            return qp[i];
        }
    }

    // called with invalid nic, assuming allocateConnection is called only for connected nics
    VERIFY(false, "access invalid nic({})", nic);

    // never get here
    return qp[0];
}

std::ostream& operator<<(std::ostream& os, const std::set<HCL_HwModuleId>& hwModules)
{
    unsigned vecCount = 1;
    for (const HCL_HwModuleId moduleId : hwModules)
    {
        os << moduleId << (vecCount < hwModules.size() ? ", " : "");
        vecCount++;
    }

    return os;
}
