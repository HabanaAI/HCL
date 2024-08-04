#pragma once

#include <cstdint>                                  // for uint32_t
#include <map>                                      // for map
#include <vector>                                   // for vector
#include <iosfwd>                                   // for ostream

#include "infra/scal/gen2_arch_common/scal_names.h" // for SchedulersIndex
#include "platform/gen2_arch_common/send_recv_aggregator.h"  // for SendRecvEntry
#include "hcl_api_types.h"                          // for HCL_Rank

struct SendRecvApiEntry;

namespace hcl
{
typedef std::map<uint32_t, SendRecvVector>
    GroupCallsAggregation;  // key => hw_mod_id, Note: There are always at most GEN2ARCH_HLS_BOX_SIZE entries in map

typedef std::vector<SendRecvArray> SendRecvArraysVector;

class GroupCalls
{
public:
    void addCall(const SendRecvApiEntry& sendRecvEntry);

    unsigned getRemoteRanksCount();

    const GroupCallsAggregation& getGroupCalls() const { return m_groupCalls; };

    SendRecvVector createScaleoutIterationEntries(const unsigned iter) const;
    const SendRecvVector& buildIterationsLayout(
        const bool     isSend,
        const HCL_Rank currRank,
        const unsigned currBox,
        const unsigned numOfBoxes,
        const HCL_Rank numOfRanks);  // builds m_orderedList in ordered manner, returns ordered list size

private:
    GroupCallsAggregation m_groupCalls;

    SendRecvVector m_orderedList;
};

typedef std::unordered_map<hcl::SchedulersIndex, GroupCalls> GroupCallsBuckets;
}  // namespace hcl

std::ostream& operator<<(std::ostream& os, const hcl::GroupCallsAggregation& groupCalls);
std::ostream& operator<<(std::ostream& os, const hcl::SendRecvArraysVector& sendRecvArraysVector);
