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
    static constexpr size_t maxEnum = static_cast<size_t>(HCL_CollectiveOp::eHCLCollectiveLastValue);
    VERIFY((size_t)hclCollectiveOp < maxEnum);
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
NicQPs& GaudiNicQPs::operator[](uint8_t nic)
{
    // search for nic nic_ap_data entry
    for (size_t i = 0; i < MAX_COMPACT_RANK_INFO_NICS; i++)
    {
        if (this->qp[i].nic == nic)
        {
            return qp[i];
        }
    }

    // log existing QPs to help debug
    for (size_t i = 0; i < MAX_COMPACT_RANK_INFO_NICS; i++)
    {
        LOG_HCL_ERR(HCL, "access invalid nic({}). Valid {}/{}", nic, i, this->qp[i].nic);
    }
    // called with invalid nic, assuming allocateQp is called only for connected nics
    VERIFY(false, "access invalid nic({})", nic);

    // never get here
    return qp[0];
}

BackupGaudiNicQPs::BackupGaudiNicQPs()
{
    for (unsigned nic = 0; nic < MAX_COMPACT_RANK_BACKUP_NICS; nic++)
    {
        qp[nic].nic = INVALID_NIC;
    }
}

NicQPs& BackupGaudiNicQPs::operator[](int8_t nic)
{
    // search for existing entry with nic of empty entry
    for (size_t i = 0; i < MAX_COMPACT_RANK_BACKUP_NICS; i++)
    {
        // if nic entry exists - return entry
        if (this->qp[i].nic == nic)
        {
            return qp[i];
        }
        // if empty entry -> set to nic and return entry
        if (this->qp[i].nic == INVALID_NIC)
        {
            this->qp[i].nic = nic;
            return qp[i];
        }
    }
    // all entries are full and nic not found - error
    // log existing QPs to help debug
    for (size_t i = 0; i < MAX_COMPACT_RANK_INFO_NICS; i++)
    {
        LOG_HCL_ERR(HCL, "access invalid nic({}). Valid {}/{}", nic, i, this->qp[i].nic);
    }
    VERIFY(false, "access invalid nic({})", nic);
}

namespace std
{
std::ostream& operator<<(std::ostream& os, const std::vector<uint32_t>& uint32Vec)
{
    std::stringstream ss;
    std::copy(uint32Vec.begin(), uint32Vec.end(), std::ostream_iterator<decltype(*uint32Vec.begin())>(ss, ","));
    os << ss.str();
    return os;
}

std::ostream& operator<<(std::ostream& os, const std::unordered_set<uint32_t>& uint32UnorderedSet)
{
    std::stringstream        ss;
    const std::set<uint32_t> orderedSet(uint32UnorderedSet.begin(), uint32UnorderedSet.end());
    std::copy(orderedSet.begin(), orderedSet.end(), std::ostream_iterator<decltype(*orderedSet.begin())>(ss, ","));
    os << ss.str();
    return os;
}
}  // namespace std