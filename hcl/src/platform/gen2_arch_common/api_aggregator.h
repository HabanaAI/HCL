#pragma once

#include <cstdint>                                   // for uint64_t
#include <map>                                       // for map
#include <deque>                                     // for deque
#include <vector>                                    // for vector
#include "group_calls.h"                             // for GroupCalls
#include "hcl_api_entry.h"                           // for SendRecvMemCopyE...
#include "infra/scal/gen2_arch_common/scal_names.h"  // for SchedulersIndex
#include "infra/futex.h"                             // for Futex
#include "hcl_collective_params.h"

class IHclCollectiveRoutines;
class HclCollectiveRoutinesGen2Arch;

constexpr auto MAX_AGG_OPS = 1024;

using SendRecvApiEntries = std::vector<SendRecvApiEntry>;

class ApiAggregatorGen2Arch
{
    using IndexedGroupCalls  = std::unordered_map<hcl::SchedulersIndex, hcl::GroupCalls>;
    using sendrecv_calls_t   = std::deque<SendRecvApiEntry>;
    using collective_calls_t = std::deque<HclCollectiveParams>;
    using type_sendrecv_map  = std::map<ApiType, sendrecv_calls_t>;
    using comms_t            = std::unordered_set<HCL_Comm>;
    using ranks_t            = std::set<HCL_Rank>;
    using comm_ranks_t       = std::unordered_map<HCL_Comm, ranks_t>;
    using comm_groupcall_map = std::unordered_map<HCL_Comm, IndexedGroupCalls>;
    using memcpy_calls_t     = std::vector<SendRecvMemCopyEntry>;

public:
    ApiAggregatorGen2Arch(HclCollectiveRoutinesGen2Arch* collectiveRoutines);
    virtual ~ApiAggregatorGen2Arch() = default;

    hcclResult_t addSendRecvApiCall(HCL_Rank myRank, const SendRecvApiEntry& entry);
    hcclResult_t addCollectiveApiCall(HclCollectiveParams& params);
    hcclResult_t addGroupStart();
    hcclResult_t addGroupEnd();

protected:
    void onHandleSendRecvEntry(SendRecvApiEntry& entry);
    void handleSelfSendRecv();
    bool checkCallsCounter();

    uint64_t     checkGroupCollectiveDependency();
    hcclResult_t onGroupEnd();

    [[nodiscard]] SendRecvApiEntries createScaleoutExpandedVector(const SendRecvApiEntry& entry) const;

    int      m_counter = 0;
    unsigned m_calls   = 0;

    comms_t            m_comms;
    comm_ranks_t       m_remoteRanks;
    sendrecv_calls_t   m_sendRecvStack;
    collective_calls_t m_collectiveStack;
    type_sendrecv_map  m_selfSendRecvStack;
    comm_groupcall_map m_groupCalls;
    memcpy_calls_t     m_sendRecvMemCpyVec;

    HclCollectiveRoutinesGen2Arch* m_collectiveRoutines;
};
