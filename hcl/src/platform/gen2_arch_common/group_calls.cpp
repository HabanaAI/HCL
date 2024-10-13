
#include "group_calls.h"

#include <algorithm>  // for max, none_of
#include <unordered_map>
#include <ostream>  // for operator<<, ostream

#include "hcl_api_types.h"                                   // for HCL_Rank
#include "hcl_api_entry.h"                                   // for SendRecvApiEntry
#include "hcl_utils.h"                                       // for VERIFY
#include "platform/gen2_arch_common/types.h"                 // for GEN2ARCH_HLS_BOX_SIZE
#include "hcl_log_manager.h"                                 // for LOG_*
#include "platform/gen2_arch_common/collective_utils.h"      // for getNextBox, getPrevBox
#include "platform/gen2_arch_common/send_recv_aggregator.h"  // for SendRecvEntry

using namespace hcl;

void GroupCalls::addCall(const SendRecvApiEntry& sendRecvEntry)
{
    SendRecvEntry entry;

    entry.count      = sendRecvEntry.count;
    entry.address    = sendRecvEntry.address;
    entry.dataType   = sendRecvEntry.dataType;
    entry.remoteRank = sendRecvEntry.remoteRank;
    entry.isValid    = true;

    m_groupCalls[sendRecvEntry.hwModuleID].push_back(entry);
}

unsigned GroupCalls::getRemoteRanksCount()
{
    unsigned ret = 0;
    for (auto& remoteRanks : m_groupCalls)
    {
        ret += remoteRanks.second.size();
    }

    return ret;
}

std::ostream& operator<<(std::ostream& os, const GroupCallsAggregation& groupCalls)
{
    for (const auto& mapPair : groupCalls)
    {
        os << "{ Key=" << mapPair.first << " : V=";
        os << mapPair.second;
        os << " }, ";
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const hcl::SendRecvArraysVector& sendRecvArraysVector)
{
    unsigned vecCount = 0;
    for (auto& arraySendRecv : sendRecvArraysVector)
    {
        os << "[" << vecCount << "]=[ " << arraySendRecv;
        os << "] ], ";
        vecCount++;
    }

    return os;
}

SendRecvVector GroupCalls::createScaleoutIterationEntries(const unsigned iter) const
{
    // if we still have something to send/recv put it otherwise its empty result
    SendRecvVector iterationRanksVector;
    if (iter < m_orderedList.size())
    {
        iterationRanksVector.push_back(m_orderedList[iter]);
    }
    return iterationRanksVector;
}

const SendRecvVector& GroupCalls::buildIterationsLayout(const bool     isSend,
                                                        const HCL_Rank currRank,
                                                        const unsigned currBox,
                                                        const unsigned numOfBoxes,
                                                        const HCL_Rank numOfRanks)
{
    LOG_HCL_TRACE(HCL,
                  "isSend={}, currRank={}, currBox={}, numOfBoxes={}, numOfRanks={}",
                  isSend,
                  currRank,
                  currBox,
                  numOfBoxes,
                  numOfRanks);

    // Convert aggregation groups into a map of all send/rec entries
    // Key => rank, Value => vector of entries to send / recv from that rank
    std::map<HCL_Rank, SendRecvVector> orderedMap;

    for (unsigned hwModuleId = 0; hwModuleId < GEN2ARCH_HLS_BOX_SIZE; hwModuleId++)
    {
        if (m_groupCalls.count(hwModuleId))
        {
            const SendRecvVector& ranksVectorPerHwModule = m_groupCalls.at(hwModuleId);

            for (const SendRecvEntry& entry : ranksVectorPerHwModule)
            {
                VERIFY(entry.isValid, "Invalid entry");
                const HCL_Rank remoteRank = entry.remoteRank;
                orderedMap[remoteRank].push_back(entry);
            }
        }
    }
    // now iterate over boxes. For send -> go right from our current box until wrap around. For recv -> go left until
    // wrap around
    const unsigned int ranksPerBox = numOfRanks / numOfBoxes;
    LOG_HCL_TRACE(HCL, "isSend={}, numOfBoxes={}, ranksPerBox={}", isSend, numOfBoxes, ranksPerBox);

    // calc send / recv ranks order
    unsigned int boxIter = currBox;
    while ((boxIter = isSend ? getNextBox(boxIter, numOfBoxes) : getPrevBox(boxIter, numOfBoxes)) != currBox)
    {
        HCL_Rank firstRankInBox = boxIter * ranksPerBox;
        HCL_Rank lastRankInBox  = boxIter * ranksPerBox + ranksPerBox - 1;
        LOG_HCL_TRACE(HCL,
                      "isSend={}, boxIter={}, firstRankInBox={}, lastRankInBox={}",
                      isSend,
                      boxIter,
                      firstRankInBox,
                      lastRankInBox);
        auto it = isSend ? ((firstRankInBox > 0) ? orderedMap.upper_bound(firstRankInBox - 1) : orderedMap.begin())
                         : orderedMap.lower_bound(lastRankInBox +
                                                  1);  // try to find next rank with entries in the target box direction
        bool foundRank = false;
        if (isSend)
        {
            foundRank = it != orderedMap.end();
        }
        else
        {
            foundRank = it != orderedMap.begin();
            if (foundRank) it--;
        }
        if (foundRank)  // found at least one rank in target box direction
        {
            // process all entries for this found box
            const HCL_Rank nextRank = it->first;               // a rank with entry(ies) in box in current direction
            boxIter                 = nextRank / ranksPerBox;  // update the box number of found
            firstRankInBox          = boxIter * ranksPerBox;   // update first rank in box found
            lastRankInBox           = boxIter * ranksPerBox + ranksPerBox - 1;  // update last rank in box found
            LOG_HCL_TRACE(HCL,
                          "isSend={}, boxIter={}, firstRankInBox={}, lastRankInBox={}, nextRank={}",
                          isSend,
                          boxIter,
                          firstRankInBox,
                          lastRankInBox,
                          nextRank);
            // loop on all possible ranks in this box, and copy their entries in order to target list
            for (HCL_Rank rankInThisBox = firstRankInBox; rankInThisBox <= lastRankInBox; rankInThisBox++)
            {
                auto ranksIter = orderedMap.find(rankInThisBox);
                if (ranksIter != orderedMap.end())
                {
                    // copy all the entires for the rank found
                    const SendRecvVector& entriesForRank = ranksIter->second;
                    for (auto entry : entriesForRank)
                    {
                        m_orderedList.push_back(entry);
                        LOG_HCL_TRACE(HCL,
                                      "Adding isSend={}, entry.count={}, rankInThisBox={}, entriesForRank.size={}",
                                      isSend,
                                      entry.count,
                                      rankInThisBox,
                                      entriesForRank.size());
                    }
                    orderedMap.erase(
                        ranksIter);  // all this rank entries were copied, remove it so we find next one in order
                }
            }
            LOG_HCL_TRACE(HCL, "isSend={}, Updated m_orderedList.size={}", isSend, m_orderedList.size());
        }
        else
        {
            LOG_HCL_TRACE(HCL, "isSend={}, Nothing to process in this boxIter={}", isSend, boxIter);
        }
    }

    // end of loop - verify all entries deleted
    LOG_HCL_TRACE(HCL, "isSend={}, processed all entries, orderedMap.size={}", isSend, orderedMap.size());
    for (const auto& iter : orderedMap)
    {
        LOG_HCL_ERR(HCL, "Invalid entries: isSend={}, orderedMap[ {}, {}]", isSend, iter.first, iter.second);
    }
    VERIFY(orderedMap.size() == 0, "iterations map should be empty, orderedMap.size={}", orderedMap.size());

    return m_orderedList;
}
