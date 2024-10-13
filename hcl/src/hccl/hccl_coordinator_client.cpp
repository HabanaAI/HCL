/******************************************************************************
 * Copyright (C) 2021 Habana Labs, Ltd. an Intel Company
 * All Rights Reserved.
 *
 * Unauthorized copying of this file or any element(s) within it, via any medium
 * is strictly prohibited.
 * This file contains Habana Labs, Ltd. proprietary and confidential information
 * and is subject to the confidentiality and license agreements under which it
 * was provided.
 *
 ******************************************************************************/

#include "hccl_coordinator_client.h"

#include <unistd.h>   // for close
#include <algorithm>  // for fill, copy, max
#include <cstdint>    // for uint32_t
#include <string>     // for string
#include <chrono>     // for system_clock

#include "hccl_helpers.h"     // for RETURN_ON_ERROR, RETURN_ON_COND
#include "hcl_tcp_utils.h"    // for sendAllToSocket, recvAllFrom...
#include "hcl_utils.h"        // for VERIFY, LOG_HCL_ERR
#include "network_utils.h"    // for address_to_string
#include "hcl_log_manager.h"  // for LOG_ERR, LOG_DEBUG
#include "hcl_types.h"        // for RankInfo

HcclCoordinatorClient::HcclCoordinatorClient(int nranks, HCL_Rank rank, const internal_unique_id_t* internalUniqueId)
: m_rank(rank), m_nranks(nranks)
{
    if (GCFG_HCL_NULL_SUBMIT.value()) return;
    openSocketWithCoordinator(m_mainSocket, internalUniqueId, BS_SEND_SOCKET);
    openSocketWithCoordinator(m_asyncRecvSocket, internalUniqueId, BS_RECV_SOCKET);
    openSocketWithCoordinator(m_logSocket, internalUniqueId, BS_LOG_SOCKET);

    if (setNonBlockingSocket(m_logSocket) == false)
    {
        LOG_HCL_ERR(HCL, "Failed to set non-blocking mode on collective log socket({}), closing it", m_logSocket);
        close(m_logSocket);
        m_logSocket = -1;
    }

    m_threadManager.createAsyncThread(m_asyncRecvSocket, m_rank);

    m_sendSequence.resize(m_nranks, 0);
    m_recvSequence.resize(m_nranks, 0);
}

void HcclCoordinatorClient::openSocketWithCoordinator(int&                        newSocket,
                                                      const internal_unique_id_t* internalUniqueId,
                                                      bootstrapSocketType         type)
{
    sockaddr_t address(internalUniqueId->address);

    newSocket = socketConnect(address);
    VERIFY(newSocket >= 0, "Failed to connect to coordinator");

    msg_header_t hdr {COMM_INIT_NEW_CONN, 0, sizeof(hcclBsCommInfo)};
    if (!sendAllToSocket(newSocket, reinterpret_cast<const void*>(&hdr), sizeof(hdr)))
    {
        VERIFY(false, "Sending new bootstrap connection header to coordinator failed");
    }

    hcclBsCommInfo commSendInfo {m_nranks, type, m_rank};
    if (!sendAllToSocket(newSocket, reinterpret_cast<const void*>(&commSendInfo), sizeof(commSendInfo)))
    {
        VERIFY(false, "Sending new bootstrap connection info to coordinator failed");
    }

    LOG_HCL_TRACE(HCL,
                  "Rank({}) connected with {} socket to coordinator successfully",
                  m_rank,
                  type == BS_SEND_SOCKET   ? "send"
                  : type == BS_RECV_SOCKET ? "async recv"
                                           : "collective log");
}

bool HcclCoordinatorClient::destroy()
{
    // Stop async thread
    m_threadManager.destroy();

    if (m_mainSocket > 0)
    {
        // Close bootstrap network
        closeBootstrapNetwork();

        close(m_mainSocket);
        m_mainSocket = -1;
    }

    if (m_asyncRecvSocket > 0)
    {
        close(m_asyncRecvSocket);
        m_asyncRecvSocket = -1;
    }

    if (m_logSocket > 0)
    {
        close(m_logSocket);
        m_logSocket = -1;
    }

    return true;
}

bool HcclCoordinatorClient::commInitHandshake1(int                          nranks,
                                               RankInfoHeader&              myRankInfo,
                                               std::vector<RankInfoHeader>& ranksInfo)
{
    msg_header_t hdr {COMM_INIT_HANDSHAKE1, 0, sizeof(RankInfoHeader)};
    size_t       bytesToRecv = nranks * sizeof(RankInfoHeader);

    if (!bootstrapMsgExchange(m_mainSocket,
                              hdr,
                              (void*)&myRankInfo,
                              sizeof(RankInfoHeader),
                              ranksInfo.data(),
                              bytesToRecv))
    {
        LOG_HCL_ERR(HCL, "rank={} bootstrap exchange with Msg id={} failed", m_rank, hdr.id);
        return false;
    }

    return true;
}

bool HcclCoordinatorClient::commInitHandshake2(int                                      nranks,
                                               void*                                    myRankInfo,
                                               uint32_t                                 rankInfoBufferSize,
                                               std::vector<RemoteDeviceConnectionInfo>& remoteDevicesInfo)
{
    msg_header_t hdr {COMM_INIT_HANDSHAKE2, 0, rankInfoBufferSize};
    size_t       bytesToRecv = nranks * sizeof(RemoteDeviceConnectionInfo);

    LOG_HCL_DEBUG(HCL, "[CLIENT] expecting receive Bytes: {}", bytesToRecv);

    if (!bootstrapMsgExchange(m_mainSocket, hdr, myRankInfo, rankInfoBufferSize, remoteDevicesInfo.data(), bytesToRecv))
    {
        LOG_HCL_ERR(HCL, "rank={} bootstrap exchange with Msg id={} failed", m_rank, hdr.id);
        return false;
    }

    bool bootstrapValidationError = false;
    if (!recvAllFromSocket(m_mainSocket, &bootstrapValidationError, sizeof(bootstrapValidationError)))
    {
        LOG_HCL_ERR(HCL, "Failed to receive bootstrapValidationError bit");
        return false;
    }
    if (bootstrapValidationError)  // some rank failed in the process, need to exit
    {
        LOG_HCL_CRITICAL(HCL, "validation error occurred between handshakes, exiting");
        return false;
    }

    return true;
}

bool HcclCoordinatorClient::bootstrapMsgExchange(int           coordinatorSocket,
                                                 msg_header_t& hdr,
                                                 const void*   msg,
                                                 size_t        msgSize,
                                                 void*         recvBuffer,
                                                 size_t        recvSize)
{
    if (!sendAllToSocket(coordinatorSocket, reinterpret_cast<const void*>(&hdr), sizeof(msg_header_t)))
    {
        LOG_HCL_ERR(HCL, "Socket send failed...");
        return false;
    }

    LOG_HCL_DEBUG(HCL, "[CLIENT] Bytes sent: {}", sizeof(hdr));

    if (!sendAllToSocket(coordinatorSocket, msg, msgSize))
    {
        LOG_HCL_ERR(HCL, "Socket send failed...");
        return false;
    }

    LOG_HCL_DEBUG(HCL, "[CLIENT] Bytes sent: {}", msgSize);

    if (!recvAllFromSocket(coordinatorSocket, recvBuffer, recvSize))
    {
        LOG_HCL_ERR(HCL, "[CLIENT] Failed to receive...");
        return false;
    }

    LOG_HCL_DEBUG(HCL, "[CLIENT] Bytes received: {}", recvSize);

    return true;
}

bool HcclCoordinatorClient::sendGeneralMsg(int coordinatorSocket, bootstrap_hdr_id_t headerId)
{
    msg_header_t                     hdr {headerId, 0, sizeof(hccl_bootstrap_general_payload_t)};
    hccl_bootstrap_general_payload_t msg {m_rank};
    bool                             finalized = false;

    if (!bootstrapMsgExchange(coordinatorSocket, hdr, &msg, sizeof(msg), &finalized, sizeof(finalized)))
    {
        LOG_HCL_ERR(HCL, "rank={} bootstrap exchange with Msg id={} failed", m_rank, headerId);
        return false;
    }

    if (finalized == false)
    {
        LOG_HCL_DEBUG(HCL, "rank={} bootstrap sync for Msg id={} failed", m_rank, headerId);
        return false;
    }

    return true;
}

bool HcclCoordinatorClient::closeBootstrapNetwork()
{
    LOG_HCL_DEBUG(HCL, "Sync bootstrap - Comm Destroy");
    if (GCFG_HCL_NULL_SUBMIT.value()) return true;
    return sendGeneralMsg(m_mainSocket, BOOTSTRAP_COMM_DESTROY);
}

bool HcclCoordinatorClient::syncBetweenRanks()
{
    LOG_HCL_DEBUG(HCL, "Sync using bootstrap");
    if (GCFG_HCL_NULL_SUBMIT.value()) return true;
    return sendGeneralMsg(m_mainSocket, SYNC_BETWEEN_RANKS);
}

hcclResult_t HcclCoordinatorClient::sendToRank(HCL_Rank peer, void* data, uint32_t size)
{
    LOG_HCL_TRACE(HCL, "peer={}, data={:p}, size={}", peer, data, size);
    msg_header_t hdr {DATA_BETWEEN_RANKS, m_sendSequence[peer], size, m_rank, peer};
    m_sendSequence[peer]++;

    RETURN_ON_ERROR(sendToCoordinator(m_mainSocket, &hdr, sizeof(hdr)), "Send hdr to coordinator failed.");

    RETURN_ON_ERROR(sendToCoordinator(m_mainSocket, data, size), "Send data to coordinator failed.");

    bool ackValue = false;
    RETURN_ON_ERROR(recvFromCoordinator(m_mainSocket, &ackValue, sizeof(ackValue)),
                    "Receive ACK from coordinator failed.");

    RETURN_ON_COND(ackValue == false, hcclInternalError, "Receive ACK with unexpected value");

    return hcclSuccess;
}

hcclResult_t HcclCoordinatorClient::sendToCoordinator(int socket, void* data, uint64_t size)
{
    if (!sendAllToSocket(socket, reinterpret_cast<const void*>(data), size))
    {
        LOG_HCL_ERR(HCL, "Sending {}, bytes to coordinator failed.", size);
        return hcclSocketError;
    }

    return hcclSuccess;
}

hcclResult_t HcclCoordinatorClient::recvFromCoordinator(int socket, void* data, uint64_t size)
{
    if (!recvAllFromSocket(socket, reinterpret_cast<void*>(data), size))
    {
        LOG_HCL_ERR(HCL, "Receiving {} bytes from coordinator failed.", size);
        return hcclSocketError;
    }

    return hcclSuccess;
}

hcclResult_t HcclCoordinatorClient::sendRecvFromRanks(UniqueSortedVector& nonPeerRemoteRanks,
                                                      std::vector<void*>& recvBuffers,
                                                      std::vector<void*>& sendBuffers,
                                                      size_t              sendRecvBufSize,
                                                      HCL_Comm            comm)
{
    LOG_HCL_TRACE(HCL_COORD, "comm: {}  nonPeers: {}  send_recv_size: {}", comm, nonPeerRemoteRanks, sendRecvBufSize);
    uint32_t ranksCounter = 0;

    LOG_HCL_TRACE(HCL, "Exchanging connections info from remote ranks - async recv");
    std::vector<std::unique_ptr<hcclHandle>> recvHandles;
    for (const HCL_Rank remoteRank : nonPeerRemoteRanks)
    {
        recvHandles.emplace_back(std::make_unique<hcclHandle>());

        void* recvBuffer = recvBuffers[ranksCounter++];
        LOG_HCL_TRACE(HCL,
                      "Calling recvFromRankAsync, comm({}), remoteRank({}), recvBuffer={:p}, recvSize={}",
                      comm,
                      remoteRank,
                      recvBuffer,
                      sendRecvBufSize);
        const hcclResult_t ret = recvFromRankAsync(recvBuffer, sendRecvBufSize, remoteRank, &(*(recvHandles.back())));
        VERIFY(ret == hcclSuccess, "recvFromRankAsync RankInfo failed, ret={}, remoteRank={}", ret, remoteRank);
    }

    LOG_HCL_TRACE(HCL, "Exchanging connections info from remote ranks - sync send");
    ranksCounter = 0;
    for (const HCL_Rank remoteRank : nonPeerRemoteRanks)
    {
        void* sendBuffer = sendBuffers[ranksCounter++];
        LOG_HCL_TRACE(HCL,
                      "Calling sendToRank, comm({}), remoteRank({}), sendBuffer={:p}, sendSize={}",
                      comm,
                      remoteRank,
                      sendBuffer,
                      sendRecvBufSize);
        const hcclResult_t ret = sendToRank(remoteRank, sendBuffer, sendRecvBufSize);
        VERIFY(ret == hcclSuccess, "sendToRank RankInfo failed, ret{}, remoteRank={}", ret, remoteRank);
    }

    LOG_HCL_TRACE(HCL, "Exchanging connections info from remote ranks - wait for recv");
    for (const HCL_Rank remoteRank : nonPeerRemoteRanks)
    {
        LOG_HCL_TRACE(HCL, "Calling waitForHandle & updateRankQps, comm={}, remoteRank={}", comm, remoteRank);

        VERIFY(recvHandles.front()->internalHandle.waitForHandle(),
               "waitForHandle RankInfo failed, remoteRank={}",
               remoteRank);
        recvHandles.erase(recvHandles.begin());  // call dtor
    }
    VERIFY(recvHandles.size() == 0, "recvHandles is not empty, {}", recvHandles.size());

    return hcclSuccess;
}

void HcclCoordinatorClient::synchronizeRemoteRanks(const HCL_Comm comm, const UniqueSortedVector& remoteRanks)
{
    // This section synchronize all the remote ranks using the coordinator
    LOG_HCL_TRACE(HCL_COORD, "comm={}, remoteRanks={}", comm, remoteRanks);

    std::vector<std::unique_ptr<hcclHandle>> recvHandles;
    std::vector<int>                         recvAckKeys(remoteRanks.size(), 0);
    unsigned                                 recvAckCount = 0;
    for (const HCL_Rank remoteRank : remoteRanks)
    {
        LOG_HCL_TRACE(HCL, "Calling recvFromRankAsync ack, comm={}, remoteRank={}", comm, remoteRank);

        recvHandles.emplace_back(std::make_unique<hcclHandle>());
        int*               ackPtr(&recvAckKeys[recvAckCount++]);
        const hcclResult_t ret = recvFromRankAsync(ackPtr, sizeof(*ackPtr), remoteRank, &(*(recvHandles.back())));
        VERIFY(ret == hcclSuccess, "recvFromRankAsync ack failed, ret={}, remoteRank={}", ret, remoteRank);
    }

    LOG_HCL_TRACE(HCL, "Synchronize with all remote ranks - sync send");
    static int ackKey = 0xABC;
    for (const HCL_Rank remoteRank : remoteRanks)
    {
        LOG_HCL_TRACE(HCL, "Calling sendToRank ack, comm={}, remoteRank={}", comm, remoteRank);

        const hcclResult_t ret = sendToRank(remoteRank, &ackKey, sizeof(ackKey));
        VERIFY(ret == hcclSuccess, "sendToRank ack failed, ret={}, remoteRank={}", ret, remoteRank);
    }

    LOG_HCL_TRACE(HCL, "Synchronize with all remote ranks - wait for recv");
    recvAckCount = 0;
    for (const HCL_Rank remoteRank : remoteRanks)
    {
        LOG_HCL_TRACE(HCL, "Calling waitForHandle ack, comm={}, remoteRank={}", comm, remoteRank);

        const int* ackPtr(&recvAckKeys[recvAckCount++]);
        VERIFY(recvHandles.front()->internalHandle.waitForHandle(),
               "waitForHandle ack failed, remoteRank={}",
               remoteRank);
        VERIFY(*ackPtr == ackKey,
               "ackKey verification failed, received key=0x{:x} from remoteRank={}, expected key=0x{}",
               *ackPtr,
               remoteRank,
               ackKey);
        recvHandles.erase(recvHandles.begin());  // call dtor
        LOG_HCL_TRACE(HCL, "waitForHandle ack completed successfully, comm={}, remoteRank={}", comm, remoteRank);
    }

    VERIFY(recvHandles.size() == 0, "After ack recvHandles is not empty, {}", recvHandles.size());
}

hcclResult_t HcclCoordinatorClient::recvFromRankAsync(void* data, int size, HCL_Rank peer, hcclHandle* handle)
{
    m_threadManager.pushAsyncJob(TCP_RECV, size, data, peer, m_recvSequence[peer], handle);
    m_recvSequence[peer]++;

    return hcclSuccess;
}

/**
 * @brief send collective log message to coordinator
 *        operation is non-blocking
 *
 * @return hcclSuccess on success
 * @return hcclSocketError on failure
 */
hcclResult_t HcclCoordinatorClient::sendCollectiveLog(const HCL_CollectiveOp op,
                                                      const size_t           count,
                                                      const hcclDataType_t   datatype,
                                                      const hcclRedOp_t      reduceOp,
                                                      const HCL_Rank         peer,
                                                      const HCL_Rank         root)
{
    CollectiveLogMessage msg {m_rank, op, {count, datatype, reduceOp, peer, root}};
    return sendCollectiveLogMsg(msg);
}

hcclResult_t HcclCoordinatorClient::sendCollectiveLogErr()
{
    CollectiveLogMessage msg {m_rank, true};
    return sendCollectiveLogMsg(msg);
}

hcclResult_t HcclCoordinatorClient::sendCollectiveLogMsg(CollectiveLogMessage& msg)
{
    // failed to set non-blocking mode on log socket
    if (m_logSocket == -1)
    {
        return hcclSocketError;
    }

    // take current time since epoch, in milliseconds
    const std::chrono::system_clock::time_point current = std::chrono::system_clock::now();
    const std::chrono::milliseconds             ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(current.time_since_epoch());

    // create header & update msg body
    msg_header_t hdr {COLLECTIVE_LOG, 0, sizeof(CollectiveLogMessage), 0, 0};
    msg.timestamp = ms.count();

    // send header
    RETURN_ON_ERROR(sendToCoordinator(m_logSocket, &hdr, sizeof(hdr)), "Send hdr to coordinator failed.");
    // send body
    RETURN_ON_ERROR(sendToCoordinator(m_logSocket, &msg, sizeof(CollectiveLogMessage)),
                    "Send collective log to coordinator failed.");

    return hcclSuccess;
}
