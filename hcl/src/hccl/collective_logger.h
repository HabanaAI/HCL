/******************************************************************************
 * Copyright (C) 2023 Habana Labs, Ltd. an Intel Company
 * All Rights Reserved.
 *
 * Unauthorized copying of this file or any element(s) within it, via any medium
 * is strictly prohibited.
 * This file contains Habana Labs, Ltd. proprietary and confidential information
 * and is subject to the confidentiality and license agreements under which it
 * was provided.
 *
 ******************************************************************************/

#pragma once

#include <deque>                    // for deque
#include <unordered_set>            // for unordered_set
#include <unordered_map>            // for unordered_map
#include <array>                    // for array
#include <functional>               // for hash

#include "hccl_internal_defs.h"     // for hcclOp


namespace std
{
/**
 * @brief hash function for the CollectiveParamsSignature struct
 * so it can be used as unordered_map key
 */
template <>
struct hash<CollectiveParamsSignature>
{
    size_t operator()(const CollectiveParamsSignature& k) const
    {
        // compute hash values for each field,
        // and combine them using XOR and bit shift
        size_t res = (hash<size_t>()(k.count) ^ (hash<int>()(k.datatype) << 1));
        res        = res ^ (hash<int>()(k.reduceOp) << 1);
        res        = res ^ (hash<int>()(k.root) << 1);

        return res;
    }
};

/**
 * @brief hash function for the SendRecvSignature struct
 * so it can be used as unordered_map key
 */
template<>
struct hash<SendRecvSignature>
{
    size_t operator()(const SendRecvSignature& k) const
    {
        // compute hash values for each field,
        // and combine them using XOR and bit shift
        size_t res = (hash<size_t>()(k.sender) ^ (hash<int>()(k.receiver) << 1));
        res        = res ^ (hash<int>()(k.count) << 1);
        res        = res ^ (hash<int>()(k.datatype) << 1);

        return res;
    }
};
}   // end namespace std


/**
 * @brief collective call log entry for a call with specific signature
 * holds the callers list and timestamps of first and last call
 */
struct CollectiveCallEntry
{
    std::unordered_set<int> callers;      // list of calling ranks
    int64_t                 first;        // timestamp of first call
    int64_t                 last;         // timestamp of last call
};

/**
 * @brief holds all the calls for collective call with params as call signature key
 *
 * CollectiveCallEntry's are stored in a deque since entries are added to end,
 * and removed from front once all ranks called the API
 *
 * the API params 'signature' is used as the key in unordered_map to store all API calls with same signature
 */
typedef std::unordered_map<CollectiveParamsSignature, std::deque<CollectiveCallEntry>> CollectiveLogCounter;

/**
 * @brief send/recv call log entry for a send-recv pair with specific signature
 * holds the sender and receiver ranks and timestamps of send/recv
 */
struct SendRecvCallEntry
{
    int64_t sendTime = std::numeric_limits<int64_t>::min();  // send timestamp
    int64_t recvTime = std::numeric_limits<int64_t>::min();  // receive timestamp
};

/**
 * @brief holds send/recv call pairs with params as call signature key
 *
 * CollectiveCallEntry's are stored in a deque since entries are added to end,
 * and removed from front once all ranks called the API
 *
 * the API params 'signature' is used as the key in unordered_map to store all API calls with same signature
 */
typedef std::unordered_map<SendRecvSignature, std::deque<SendRecvCallEntry>> SendRecvLogCounter;

/**
 * @brief CollectiveLogger handles all collective logs reported to coordinator
 * it handle each collective log message at arrive
 * it reports when API operation is done and issue warning if a time drift between ranks is discovered
 */
class CollectiveLogger
{
// public methods
public:
    void processLogMessage(const CollectiveLogMessage& msg);
    void setCommSize(const uint32_t size);

    // constructors, destructor
    CollectiveLogger()                                   = default;
    CollectiveLogger(CollectiveLogger&&)                 = delete;
    CollectiveLogger(const CollectiveLogger&)            = delete;
    CollectiveLogger& operator=(CollectiveLogger&&)      = delete;
    CollectiveLogger& operator=(const CollectiveLogger&) = delete;
    ~CollectiveLogger();

    // private methods
private:
    bool isCollectiveOp(hcclOp op) const { return op <= eHCCLCollectiveMax; }
    void processCollectiveOp(const CollectiveLogMessage& msg);
    void processSendRecvOp(const CollectiveLogMessage& msg);

// private members
private:
    /**
     * @brief collective calls log counters database
     * there is one array entry for each collective API defined in hcclOp enum
     */
    std::array<CollectiveLogCounter, eHCCLCollectiveMax + 1> m_collectiveCounters;

    /**
     * @brief send/recv log counters database
     */
    SendRecvLogCounter m_sendRecvCounter;

    /**
     * @brief comm size is required to track all ranks called an API
     */
    uint32_t m_commSize = 0;
};
