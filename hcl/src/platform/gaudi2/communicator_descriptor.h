#pragma once

#include <cstddef>                                  // for size_t
#include <cstdint>                                  // for uint8_t, uint32_t
#include <array>                                    // for array
#include <list>                                     // for list, list<>::iter...
#include <set>                                      // for set
#include <utility>                                  // for pair
#include <vector>                                   // for vector
#include "hcl_api_types.h"                          // for HCL_Comm, HCL_Rank
#include "hcl_types.h"
#include "sched_pkts.h"                             // for g2fw
#include "platform/gen2_arch_common/types.h"        // for QpInfo
#include "hcl_dynamic_communicator.h"               // for HclDynamicCommunicator

constexpr size_t g_qpnTableSize = 11;

static inline g2fw::nic_coll_ctxt_dword_t createEmptyRemoteDescriptor()
{
    g2fw::nic_coll_ctxt_dword_t descriptor;
    descriptor.dword_value         = 0;
    descriptor.remote_rank_enable0 = 0;
    descriptor.remote_rank_enable1 = 0;
    descriptor.remote_rank_enable2 = 0;
    descriptor.remote_rank_enable3 = 0;
    descriptor.remote_rank_enable4 = 0;
    descriptor.remote_rank_enable5 = 0;
    descriptor.remote_rank_enable6 = 0;
    descriptor.remote_rank_enable7 = 0;

    return descriptor;
}

class ScaleUpQpInfo : public QpInfo
{
public:
    ScaleUpQpInfo() : QpInfo(), remoteDescriptor(createEmptyRemoteDescriptor()) {}

    ScaleUpQpInfo(uint32_t _qpn, uint32_t _qpi) : QpInfo(_qpn, _qpi), remoteDescriptor(createEmptyRemoteDescriptor()) {}

protected:
    g2fw::nic_coll_ctxt_dword_t remoteDescriptor;

public:
    inline g2fw::nic_coll_ctxt_dword_t& getRemoteDescriptor() { return remoteDescriptor; };
};

using ScaleOutQpInfo = QpInfo;

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
    CommunicatorDescriptor(uint8_t collectiveContextIndex);

    void registerQPs(HCL_Comm comm, uint8_t nic, unsigned qpi, const QpsVector& qps);

    std::pair<unsigned, uint32_t> useQP(HCL_Comm comm, uint8_t nic);

    unsigned getCommDescIndex(HCL_Comm comm);

    bool isActive(HCL_Comm comm, uint8_t nic);

    g2fw::nic_coll_ctxt_dword_t& getRemoteDescriptor(HCL_Comm comm, uint8_t nic);

    uint32_t getQP(HCL_Comm comm, uint8_t nic);
    uint32_t getQPi(HCL_Comm comm, uint8_t nic);

    inline bool requiresLruUpdate(const HCL_Comm comm) const
    {
        return (!m_commDownloaded[comm] || (m_lru.size() == g_qpnTableSize));
    }

    inline void markCommDownload(const HCL_Comm comm) { m_commDownloaded[comm] = true; }

private:
    void           resizeDBs(HCL_Comm comm);
    ScaleUpQpInfo* seek(HCL_Comm comm, uint8_t nic);

    std::vector<std::array<ScaleUpQpInfo, MAX_NICS_GEN2ARCH>> m_qpsPerNic;

    std::vector<bool> m_commDownloaded; // Mark per comm when commands were download

    uint8_t m_collectiveContextIndex;
    LRU     m_lru;
};

/** example
* rsi = 5
* NIC 8: qps = {1, 2, 3, 4}
* NIC 22: qps = {10, 11, 16, 18}
* NIC 23: qps = {101, 102, 103, 104}

* qps_quad RS send = {5, 3, 16, 103}
* qps_quad RS recv = {5, 1, 10, 101}
* qps_quad AG send = {5, 4, 18, 104}
* qps_quad AG recv = {5, 2, 11, 102}
* qp indices: {RS recv, AG recv, RS send, AG send}
*/
class ScaleOutQPsTracker
{
public:
    ScaleOutQPsTracker();

    void registerQPs(HclDynamicCommunicator& dynamicComm,
                     uint8_t                 SubNicIndex,
                     HCL_Rank                remoteRank,
                     unsigned                qpi,
                     const QpsVector&        qps);
    void allocatCommQPs(HCL_Comm comm, uint32_t commSize);

    uint32_t getRankQpn(HCL_Comm comm, HCL_Rank remoteRank, uint8_t subNicIndex, uint8_t qpSet);
    uint32_t getRankQpi(HCL_Comm comm, HCL_Rank remoteRank, uint8_t subNicIndex, uint8_t qpSet);

    // m_qpInfoDb[comm][remoteRank][sub_nic][qpSet] ->ScaleOutQpInfo
    std::vector<std::vector<std::array<std::array<ScaleOutQpInfo, MAX_QPS_SETS_PER_CONNECTION>, 3>>> m_qpInfoDb;
};
