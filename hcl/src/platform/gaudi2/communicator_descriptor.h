#pragma once

#include <cstddef>  // for size_t
#include <cstdint>  // for uint8_t, uint32_t
#include <array>    // for array
#include <list>     // for list, list<>::iter...
#include <set>      // for set
#include <utility>  // for pair
#include <vector>   // for vector

#include "hcl_api_types.h"                    // for HCL_Comm, HCL_Rank
#include "hcl_types.h"                        // for MAX_QPS_SETS_PER_CONNECTION
#include "g2_sched_pkts.h"                    // for g2fw
#include "platform/gen2_arch_common/types.h"  // for MAX_NICS_GEN2ARCH
#include "hcl_dynamic_communicator.h"         // for HclDynamicCommunicator

constexpr size_t g_qpnTableSize = 11;

/**
 * Just a plain and simple LRU implementation, nothing to see here
 * >.>
 * <.<
 * >.<
 *
 * This implementation hopes to be heavily-focused on efficiency, where access to an Entry (which describes all of the
 * known Communicators and possible comm_desc_index values (the index in the FW's collective context's array)) is as
 * efficient as accessing its' score.
 * The complication arises with the understanding that, if an Entry doesn't have a reference to its' score iterator,
 * std::find will have to be invoked, which is O(n) and costs a lot of cycles in the normal benchmark scenario.
 *
 * At the end of the day, pointer dereference is a lot quicker than O(n). The cost is maintaining said pointers.
 */
class LRU
{
public:
    LRU();

    bool isActive(HCL_Comm comm);

    unsigned use(HCL_Comm comm);

    inline size_t size() const { return m_lruPosition.size(); }

    void resizeDB(HCL_Comm comm);

private:
    // m_lruPosition is at most g_qpnTableSize (11) items. At the front() is the newest Entry to be inserted into the
    // LRU, at the back() - the oldest.
    struct Entry;
    std::list<Entry*> m_lruPosition;
    using lruPosition_iterator = decltype(m_lruPosition)::iterator;

    struct Entry
    {
        enum
        {
            INACTIVE = 0,
            ACTIVE
        } state = INACTIVE;
        unsigned comm_desc_index;  // Index in the collective context's comm desc array. Only valid if ACTIVE.
        lruPosition_iterator lruPosition_it;  // The iterator to the scores entry. Only valid if ACTIVE.
        HCL_Comm             comm;
    };

    // A vector of all the communicators that were used by this collective context.
    std::vector<Entry> m_communicators;

    // The LRU score of the Entry
    inline unsigned getPlaceInLRU(lruPosition_iterator it);

    // Remove the back() of the scores
    size_t evict();

    size_t m_size;
};

class CommunicatorDescriptor
{
public:
    CommunicatorDescriptor(uint8_t collectiveIndex);

    void registerComm(HCL_Comm comm);

    std::pair<unsigned, uint32_t> useQP(HCL_Comm comm, uint32_t qpn);

    unsigned getCommDescIndex(HCL_Comm comm);

    bool isActive(HCL_Comm comm);

    g2fw::nic_coll_ctxt_dword_t& getRemoteDescriptor(HCL_Comm comm, uint8_t nic);

    inline bool requiresLruUpdate(const HCL_Comm comm) const
    {
        return (!m_commDownloaded[comm] || (m_lru.size() == g_qpnTableSize));
    }

    inline void markCommDownload(const HCL_Comm comm) { m_commDownloaded[comm] = true; }

private:
    std::vector<bool> m_commDownloaded;  // Mark per comm when commands were download

    std::vector<std::array<g2fw::nic_coll_ctxt_dword_t, MAX_NICS_GEN2ARCH>> m_remoteDescriptors;

    uint8_t m_collectiveContextIndex;
    LRU     m_lru;
};