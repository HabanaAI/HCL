/******************************************************************************
 * Copyright (C) 2022 Habana Labs, Ltd. an Intel Company
 * All Rights Reserved.
 *
 * Unauthorized copying of this file or any element(s) within it, via any medium
 * is strictly prohibited.
 * This file contains Habana Labs, Ltd. proprietary and confidential information
 * and is subject to the confidentiality and license agreements under which it
 * was provided.
 *
 ******************************************************************************/

#include "hlcp_client.h"
#include "hccl_helpers.h"     // for RETURN_ON_ERROR, RETURN_ON_COND
#include "hcl_utils.h"        // for VERIFY, LOG_HCL_ERR
#include "hcl_log_manager.h"  // for LOG_ERR, LOG_DEBUG
#include "hcl_types.h"        // for RankInfo

hlcp_client_t::hlcp_client_t(uint32_t nranks, HCL_Rank rank, const internal_unique_id_t* internalUniqueId)
: rank_(rank), ranks_(nranks)
{
    if (GCFG_HCL_NULL_SUBMIT.value()) return;

    gcfg_.io_threads = GCFG_HCL_HLCP_CLIENT_IO_THREADS.value();
    gcfg_.op_timeout = GCFG_HCL_HLCP_OPS_TIMEOUT.value();

    if (!start(gcfg_.io_threads))
    {
        VERIFY(false, "cannot start hlcp client");
        return;
    }

    hlcp_srv_ = internalUniqueId->address;

    rank_addr_.resize(ranks_);
    non_peers_.resize(ranks_);

    HLCP_INF("{} {} hlcp_srv: {}", this, srv_.local_addr.str(), hlcp_srv_.str());
}

void hlcp_client_t::on_command(hlcp_command_t& cmd, hlcp_t& connection)
{
    HLCP_LOG("{}", cmd.id());

    switch (state_)
    {
        case comm_data:
            cmd_comm_data_.completed_ = true;
            break;

        case qps_conf:
            cmd_qps_conf_.completed_ = true;

            state_ = conf_done;
            break;

        case conf_done:
        {
            VERIFY(cmd.id() == HLCP_NON_PEERS);

            hlcp_cmd_non_peers_t& command = (hlcp_cmd_non_peers_t&)cmd;

            HCL_Rank rank = command.param_;

            VERIFY(!non_peers_[rank].initialized, "non peer {} already initialized", rank);

            non_peers_[rank].initialized = true;

            delete &cmd;
        }
        break;

        default:
            VERIFY(false, "invalid protocol state: {}. {} remote:{} ", state_, cmd, connection->remote_addr.str());
            break;
    }

    close_connection(connection);
}

void hlcp_client_t::on_error(bool send, hlcp_command_t* cmd, const hlcp_packet_t& packet, hlcp_t& connection)
{
    HLCP_ERR("{} {} expected {} {}, connection: {}", state_, send ? "send" : "recv", cmd, packet, connection->str());

    drop_connection(connection);
}

void hlcp_client_t::on_connect(hlcp_t& connection)
{
    //
    // our server socket accepted new connection, can be from server or from other client
    // depending on state
    //
    switch (state_)
    {
        case comm_data:
            connection.receive_command(cmd_comm_data_);
            break;

        case qps_conf:
            connection.receive_command(cmd_qps_conf_);
            break;

        case conf_done:  // we can receive server SYNC connect or NON_PEER connect
            connection.receive();
            break;

        default:
            VERIFY(false, "invalid protocol state: {}. remote:{} ", state_, connection->remote_addr.str());
            break;
    }
}

void hlcp_client_t::on_message(const hlcp_message_t& msg, hlcp_t& connection)
{
    HLCP_LOG("{}", msg.id);
    switch (state_)
    {
        case conf_done:  // we can receive SYNC or NON_PEER data
        {
            if (msg.id == HLCP_NON_PEERS)
            {
                hlcp_cmd_non_peers_t* np_cmd = new hlcp_cmd_non_peers_t(msg);

                HCL_Rank rank = np_cmd->param_;

                np_cmd->payload_      = &non_peers_[rank].data;
                np_cmd->payload_size_ = msg.payload_size;

                connection.receive_payload(*np_cmd);
            }
            else if (msg.id == HLCP_SYNC)  // sync
            {
                hlcp_cmd_sync_t command(msg);
                close_connection(connection);

                HCL_Rank rank = command.param_;

                if (rank == HCL_INVALID_RANK)  // server
                {
                    cmd_sync_.completed_ = true;
                }
                else
                {
                    VERIFY(!non_peers_[rank].synched, "non peer {} already synchronized", rank);

                    non_peers_[rank].synched = true;
                }
            }
        }
        break;

        default:
            VERIFY(false, "invalid protocol state: {}. msg:{} remote:{} ", state_, msg, connection->remote_addr.str());
            break;
    }
}

bool hlcp_client_t::syncBetweenRanks()
{
    hlcp_cmd_sync_t cmd(rank_);

    RET_ON_FALSE(send_to_srv(cmd));

    wait_condition(cmd_sync_.completed_, gcfg_.op_timeout);

    HLCP_INF("completed");

    return true;
}

bool hlcp_client_t::destroy()
{
    // Stop async thread
    return true;
}

bool hlcp_client_t::commInitHandshake1(int nranks, RankInfoHeader& myRankInfo, ranks_headers_t& ranksInfo)
{
    cmd_comm_data_.payload_      = ranksInfo.data();
    cmd_comm_data_.payload_size_ = nranks * sizeof(RankInfoHeader);

    state_ = comm_data;

    hlcp_cmd_rank_data_t cmd({myRankInfo, srv_.local_addr.port(), (uint32_t)nranks});

    HLCP_INF("rank: {} hlcp_port: {} comm_size:{}",
             cmd.param_.info.hcclRank,
             cmd.param_.hlcp_port,
             cmd.param_.comm_size);

    RET_ON_FALSE(send_to_srv(cmd));

    wait_condition(cmd_comm_data_.completed_, gcfg_.op_timeout);

    for (const auto& hdr : ranksInfo)
    {
        rank_addr_[hdr.hcclRank] = hdr.caddr;
        addr_rank_.insert({rank_addr_[hdr.hcclRank].addr(), hdr.hcclRank});
    }

    HLCP_INF("completed");

    return true;
}

bool hlcp_client_t::commInitHandshake2(int               nranks,
                                       void*             myRankInfo,
                                       uint32_t          rankInfoBufferSize,
                                       remote_devices_t& remoteDevicesInfo)
{
    HLCP_LOG("");

    cmd_qps_conf_.payload_      = remoteDevicesInfo.data();
    cmd_qps_conf_.payload_size_ = nranks * sizeof(RemoteDeviceConnectionInfo);

    state_ = qps_conf;

    hlcp_cmd_qps_conf_t cmd(nranks, myRankInfo, rankInfoBufferSize);

    RET_ON_FALSE(send_to_srv(cmd));

    wait_condition(cmd_qps_conf_.completed_, gcfg_.op_timeout);

    HLCP_INF("completed");

    return true;
}

bool hlcp_client_t::send_to_rank(HCL_Rank rank, const hlcp_command_t& cmd)
{
    const auto& rank_addr = rank_addr_[rank];

    socket_t socket;

    RET_ON_FALSE(socket.connect(rank_addr, gcfg_.op_timeout));

    hlcp_t hlcp(socket);

    RET_ON_FALSE(hlcp.send_command(cmd, gcfg_.op_timeout));

    HLCP_LOG("sent:{} [{}] {}", cmd.id(), cmd.payload_size() + cmd.param_size(), socket.str());

    hlcp.recv_ack();

    return true;
}

hcclResult_t hlcp_client_t::sendRecvFromRanks(UniqueSortedVector& nonPeerRemoteRanks,
                                              std::vector<void*>& recvBuffers,
                                              std::vector<void*>& sendBuffers,
                                              size_t              sendRecvBufSize,
                                              HCL_Comm            comm)
{
    HLCP_INF("comm: {}  nonPeers: {}  send_recv_size: {}", comm, nonPeerRemoteRanks, sendRecvBufSize);

    if (!xchg_non_peer_data(nonPeerRemoteRanks, recvBuffers, sendBuffers, sendRecvBufSize, comm))
        return hcclInternalError;

    HLCP_INF("completed");

    return hcclSuccess;
}

bool hlcp_client_t::non_peer_data_ready(const UniqueSortedVector& nonPeerRemoteRanks, bool init)
{
    bool all_received = true;

    for (const auto& rank : nonPeerRemoteRanks)
    {
        all_received &= (init ? non_peers_[rank].initialized : non_peers_[rank].synched);
    }

    return all_received;
}

bool hlcp_client_t::xchg_non_peer_data(const UniqueSortedVector& nonPeerRemoteRanks,
                                       const std::vector<void*>& recvBuffers,
                                       const std::vector<void*>& sendBuffers,
                                       size_t                    sendRecvBufSize,
                                       HCL_Comm                  comm)
{
    uint32_t i = 0;
    for (const HCL_Rank remoteRank : nonPeerRemoteRanks)
    {
        hlcp_cmd_non_peers_t cmd(rank_, sendBuffers[i++], sendRecvBufSize);

        send_to_rank(remoteRank, cmd);
    }

    wait_condition(non_peer_data_ready(nonPeerRemoteRanks, true), gcfg_.op_timeout);

    i = 0;
    for (const HCL_Rank remoteRank : nonPeerRemoteRanks)
    {
        std::memcpy(recvBuffers[i++], &non_peers_[remoteRank].data, sendRecvBufSize);
    }

    return true;
}

void hlcp_client_t::synchronizeRemoteRanks(const HCL_Comm comm, const UniqueSortedVector& nonPeerRemoteRanks)
{
    HLCP_INF("comm={}, remoteRanks={}", comm, nonPeerRemoteRanks);
    VERIFY(sync_non_peers(comm, nonPeerRemoteRanks), "non peers sync failure");
    HLCP_INF("completed");
}

bool hlcp_client_t::sync_non_peers(const HCL_Comm comm, const UniqueSortedVector& nonPeerRemoteRanks)
{
    hlcp_cmd_sync_t cmd(rank_);

    for (const HCL_Rank remoteRank : nonPeerRemoteRanks)
    {
        send_to_rank(remoteRank, cmd);
    }

    wait_condition(non_peer_data_ready(nonPeerRemoteRanks, false), gcfg_.op_timeout);

    return true;
}

hcclResult_t hlcp_client_t::sendCollectiveLog(const HCL_CollectiveOp op,
                                              const size_t           count,
                                              const hcclDataType_t   datatype,
                                              const hcclRedOp_t      reduceOp,
                                              const HCL_Rank         peer,
                                              const HCL_Rank         root)
{
    CollectiveLogMessage msg {rank_, op, {count, datatype, reduceOp, peer, root}};

    if (!send_log_msg(msg)) return hcclInternalError;

    return hcclSuccess;
}

hcclResult_t hlcp_client_t::sendCollectiveLogErr()
{
    CollectiveLogMessage msg {rank_, true};

    if (!send_log_msg(msg)) return hcclInternalError;

    return hcclSuccess;
}

bool hlcp_client_t::send_to_srv(const hlcp_command_t& cmd)
{
    socket_t socket;

    RET_ON_FALSE(socket.connect(hlcp_srv_, gcfg_.op_timeout));

    hlcp_t hlcp(socket);

    RET_ON_FALSE(hlcp.send_command(cmd, gcfg_.op_timeout));

    HLCP_LOG("sent: {} [{}] {}", cmd.id(), cmd.payload_size() + cmd.param_size(), socket.str());

    hlcp.recv_ack();

    return true;
}

bool hlcp_client_t::send_log_msg(CollectiveLogMessage& msg)
{
    // take current time since epoch, in milliseconds
    const std::chrono::system_clock::time_point current = std::chrono::system_clock::now();
    const std::chrono::milliseconds             ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(current.time_since_epoch());

    msg.timestamp = ms.count();

    hlcp_cmd_log_msg_t cmd(msg);

    return send_to_srv(cmd);
}
