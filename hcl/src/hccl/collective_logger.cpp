#include "collective_logger.h"

CollectiveLogger::~CollectiveLogger()
{
    LOG_INFO(HCL_COORD,
             "Collective Counters [{}, {}, {}, {}, {}, {}]",
             m_collectiveCounters[eHCCLReduce].size(),
             m_collectiveCounters[eHCCLAllReduce].size(),
             m_collectiveCounters[eHCCLReduceScatter].size(),
             m_collectiveCounters[eHCCLBroadcast].size(),
             m_collectiveCounters[eHCCLAllGather].size(),
             m_collectiveCounters[eHCCLAllToAll].size());

    for (size_t i = 0; i <= eHCCLCollectiveMax; i++)
    {
        for (auto dq : m_collectiveCounters[i])
        {
            if (dq.second.size() != 0)
            {
                LOG_ERR(HCL_COORD,
                        "Collective: Non empty deque({}) found for signature({}, {}, {}, {}, {})",
                        dq.second.size(),
                        hcclOp(i),
                        dq.first.count,
                        dq.first.datatype,
                        dq.first.reduceOp,
                        dq.first.root);
            }
        }
    }

    LOG_INFO(HCL_COORD, "send/recv Counters [{}]", m_sendRecvCounter.size());

    for (auto dq : m_sendRecvCounter)
    {
        if (dq.second.size() != 0)
        {
            LOG_ERR(HCL_COORD,
                    "send/recv: Non empty deque({}) found for signature({}, {}, {}->{})",
                    dq.second.size(),
                    dq.first.count,
                    dq.first.datatype,
                    dq.first.sender,
                    dq.first.receiver);
        }
    }
}

/**
 * @brief set the related communicator size
 * coordinator needs to set it when a new communicator is created on bootstrap network
 */
void CollectiveLogger::setCommSize(const uint32_t size)
{
    m_commSize = size;
}

/**
 * @brief process a new log message arrived from a rank in communicator
 * different processing for collective calls and sen/recv calls
 * @param msg - log message to process
 */
void CollectiveLogger::processLogMessage(const CollectiveLogMessage& msg)
{
    LOG_HCL_DEBUG(HCL_COORD,
                  "[{}]: Rank({}) called ({}, {}, {}, {}, {}, {})",
                  msg.timestamp,
                  msg.rank,
                  msg.op,
                  msg.params.count,
                  msg.params.datatype,
                  msg.params.reduceOp,
                  msg.params.peer,
                  msg.params.root);

    if (isCollectiveOp(msg.op))
    {
        processCollectiveOp(msg);
    }
    else
    {
        processSendRecvOp(msg);
    }
}

/**
 * @brief process a collective call log message
 * add to database
 * when all ranks called API - report and remove from database
 * track time drifts of each call
 *
 * @param msg
 */
void CollectiveLogger::processCollectiveOp(const CollectiveLogMessage& msg)
{
    // check if counter exist for this call signature
    if (m_collectiveCounters[msg.op].find(msg.params) == m_collectiveCounters[msg.op].end())
    {
        // first call for this signature, create deque and insert first entry
        m_collectiveCounters[msg.op][msg.params] = std::deque<CollectiveCallEntry>();
        m_collectiveCounters[msg.op][msg.params].push_back(
            {std::unordered_set<int>({msg.rank}), msg.timestamp, msg.timestamp});
        LOG_HCL_DEBUG(HCL_COORD,
                      "deque({}) created for ({}, {}, {}, {}, {}, {}), set({}) contains {} rank",
                      m_collectiveCounters[msg.op][msg.params].size(),
                      msg.op,
                      msg.params.count,
                      msg.params.datatype,
                      msg.params.reduceOp,
                      msg.params.peer,
                      msg.params.root,
                      msg.rank,
                      m_collectiveCounters[msg.op][msg.params][0].callers.size());
    }
    else    // counter found for call signature
    {
        bool found = false;     // indicate message is handled

        // calls list for the params signature
        std::deque<CollectiveCallEntry>& calls = m_collectiveCounters[msg.op][msg.params];
        LOG_HCL_DEBUG(HCL_COORD,
                      "searching deque({}), rank({}) for ({}, {}, {}, {}, {}, {}), set contains {} rank",
                      calls.size(),
                      msg.rank,
                      msg.op,
                      msg.params.count,
                      msg.params.datatype,
                      msg.params.reduceOp,
                      msg.params.peer,
                      msg.params.root,
                      m_collectiveCounters[msg.op][msg.params][0].callers.size());

        // iterate signatured calls list, check if rank called
        for (size_t i = 0; i < calls.size(); i++)
        {
            // rank did not call this entry, add rank to callers list
            if (calls[i].callers.find(msg.rank) == calls[i].callers.end())
            {
                calls[i].callers.insert(msg.rank);

                // update first/last timestamps
                if (msg.timestamp < calls[i].first)
                {
                    calls[i].first = msg.timestamp;
                }
                else if (msg.timestamp > calls[i].last)
                {
                    calls[i].last = msg.timestamp;
                }
                LOG_HCL_DEBUG(HCL_COORD,
                              "Index({}): Rank({}) called({}, {}, {}, {}, {}, {}), set contains {} ranks",
                              i,
                              msg.rank,
                              msg.op,
                              msg.params.count,
                              msg.params.datatype,
                              msg.params.reduceOp,
                              msg.params.peer,
                              msg.params.root,
                              calls[i].callers.size());

                // handle last rank call, report and remove entry from list
                if (calls[i].callers.size() == m_commSize)
                {
                    // just log it
                    LOG_INFO(HCL_COORD,
                             "All ({}) ranks called ({}, {}, {}, {}, {}, {}), first({}) - last({})",
                             m_commSize,
                             msg.op,
                             msg.params.count,
                             msg.params.datatype,
                             msg.params.reduceOp,
                             msg.params.peer,
                             msg.params.root,
                             calls[i].first,
                             calls[i].last);

                    // remove entry from deque
                    // it is expected that the first entry is the one to remove
                    // TODO: replace with error and disable collective logs
                    VERIFY(i == 0, "Collective logger lost it's mind");

                    calls.pop_front();
                }
                // check drift between ranks, issue warning if passing threshold
                else if (calls[i].last - calls[i].first > GCFG_OP_DRIFT_THRESHOLD_MS.value())
                {
                    LOG_WARN(HCL_COORD,
                             "call ({}, {}, {}, {}, {}, {}), first({}) - last({}), exceed {}ms threshold, ({}/{}) "
                             "ranks already called",
                             msg.op,
                             msg.params.count,
                             msg.params.datatype,
                             msg.params.reduceOp,
                             msg.params.peer,
                             msg.params.root,
                             calls[i].first,
                             calls[i].last,
                             GCFG_OP_DRIFT_THRESHOLD_MS.value(),
                             calls[i].callers.size(),
                             m_commSize);
                }


                // done with message, exit loop
                found = true;
                break;
            }
        }

        // if we got here and not found, it is first of new call for this signature, insert new deque entry
        if (!found)
        {
            calls.push_back({std::unordered_set<int>({msg.rank}), msg.timestamp, msg.timestamp});
            LOG_HCL_DEBUG(HCL_COORD,
                          "New call added to deque({}), Rank({}) called({}, {}, {}, {}, {}, {}), set contains {} ranks",
                          calls.size(),
                          msg.rank,
                          msg.op,
                          msg.params.count,
                          msg.params.datatype,
                          msg.params.reduceOp,
                          msg.params.peer,
                          msg.params.root,
                          calls[calls.size() - 1].callers.size());
        }
    }
}

/**
 * @brief process send/recv log message
 * add new signature (send rank, recv rank, count, data type)
 * for existing signature deque contains only
 *
 * @param msg
 */
void CollectiveLogger::processSendRecvOp(const CollectiveLogMessage& msg)
{
    bool    isSend   = true;
    int     sender   = -1;
    int     receiver = -1;
    int64_t sendTime = std::numeric_limits<int64_t>::min();  // send timestamp
    int64_t recvTime = std::numeric_limits<int64_t>::min();  // receive timestamp
    if (msg.op == eHCCLSend)
    {
        sender   = msg.rank;
        sendTime = msg.timestamp;
        receiver = msg.params.peer;
    }
    else
    {
        isSend   = false;
        sender   = msg.params.peer;
        receiver = msg.rank;
        recvTime = msg.timestamp;
    }
    const SendRecvSignature sign = {sender, receiver, msg.params.count, msg.params.datatype};

    // check if counter exist for this call signature
    if (m_sendRecvCounter.find(sign) == m_sendRecvCounter.end())
    {
        // first call for this signature, create deque and insert first entry
        m_sendRecvCounter[sign] = std::deque<SendRecvCallEntry>();
    }
    else  // counter found for call signature
    {
        // add new entry if:
        // 1. deque is empty
        // 2. send op log and deque contains send logs
        // 3. recv op log and deque contains recv logs
        if (m_sendRecvCounter[sign].size() == 0 ||
            (isSend && m_sendRecvCounter[sign][0].sendTime != std::numeric_limits<int64_t>::min()) ||
            (!isSend && m_sendRecvCounter[sign][0].recvTime != std::numeric_limits<int64_t>::min()))
        {
            m_sendRecvCounter[sign].push_back({sendTime, recvTime});
            LOG_HCL_DEBUG(HCL_COORD,
                          "send/recv deque created for ({}, {}, {}, {}->{})",
                          msg.op,
                          sign.count,
                          sign.datatype,
                          sender,
                          receiver);
            // check drift between ranks, issue warning if passing threshold
            if (m_sendRecvCounter[sign].size() > 1)
            {
                int delta = 0;
                isSend ? delta = sendTime - m_sendRecvCounter[sign][0].sendTime
                       : delta = recvTime - m_sendRecvCounter[sign][0].recvTime;
                if (delta > GCFG_OP_DRIFT_THRESHOLD_MS.value())
                {
                    LOG_WARN(HCL_COORD,
                             "send/recv ({}, {}), drift({}), exceed {}ms threshold, {} not arriving",
                             sign.count,
                             sign.datatype,
                             delta,
                             GCFG_OP_DRIFT_THRESHOLD_MS.value(),
                             isSend ? "send" : "recv");
                }
            }
        }
        // got send op log and deque contains recv op logs, or
        // got recv op log and deque contains send op logs
        // remove entry from deque and lof it
        else
        {
            // get call entry, complete info, report and remove from deque
            SendRecvCallEntry entry = m_sendRecvCounter[sign].front();
            isSend ? entry.sendTime = sendTime : entry.recvTime = recvTime;
            m_sendRecvCounter[sign].pop_front();
            LOG_INFO(HCL_COORD,
                     "Rank({}) send ({}, {}) on[{}] to rank({}) recv on[{}]",
                     sender,
                     sign.count,
                     sign.datatype,
                     entry.sendTime,
                     receiver,
                     entry.recvTime);

            // check drift between ranks, issue warning if passing threshold
            int delta = 0;
            entry.sendTime > entry.recvTime ? delta = entry.sendTime - entry.recvTime
                                            : delta = entry.recvTime - entry.sendTime;
            if (delta > GCFG_OP_DRIFT_THRESHOLD_MS.value())
            {
                LOG_WARN(HCL_COORD,
                         "send[{}]/recv[{}] ({}, {}), delta({}), exceed {}ms threshold",
                         entry.sendTime,
                         entry.recvTime,
                         sign.count,
                         sign.datatype,
                         delta,
                         GCFG_OP_DRIFT_THRESHOLD_MS.value());
            }
        }
    }
}
