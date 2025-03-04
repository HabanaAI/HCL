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
#include "hccl_helpers.h"              // for RETURN_ON_ERROR, RETURN_ON_COND
#include "hcl_utils.h"                 // for VERIFY, LOG_HCL_ERR
#include "hcl_log_manager.h"           // for LOG_ERR, LOG_DEBUG
#include "hcl_types.h"                 // for RankInfo
#include "coordinator/qp_migration.h"  // for IMigrationCallback

hlcp_client_t::hlcp_client_t(const HCL_Comm              comm,
                             const uint32_t              nranks,
                             const HCL_Rank              rank,
                             const internal_unique_id_t* internalUniqueId,
                             IMigrationCallback&         migrationCb)
: comm_id_(comm), rank_(rank), ranks_(nranks)
{
    if (GCFG_HCL_NULL_SUBMIT.value()) return;

    gcfg_.io_threads = GCFG_HCL_HLCP_CLIENT_IO_THREADS.value();
    gcfg_.op_timeout = GCFG_HCL_HLCP_OPS_TIMEOUT.value();

    if (!start(gcfg_.io_threads))
    {
        VERIFY(false, "cannot start hlcp client");
        return;
    }

    hlcp_srv_     = internalUniqueId->address;
    migration_cb_ = &migrationCb;

    HLCP_INF("{} {} hlcp_srv: {}", this, srv_.local_addr.str(), hlcp_srv_.str());
}

void hlcp_client_t::reset()
{
    rank_addr_.clear();
    rank_addr_.resize(ranks_);

    non_peers_.clear();
    non_peers_.resize(ranks_);
}

void hlcp_client_t::on_hlcp_comm_data([[maybe_unused]] hlcp_cmd_comm_data_t& cmd)
{
    cmd_comm_data_.completed_ = true;
}

void hlcp_client_t::on_hlcp_qps_conf([[maybe_unused]] hlcp_cmd_qps_conf_t& cmd)
{
    cmd_qps_conf_.completed_ = true;
}

void hlcp_client_t::on_hlcp_non_peer_data(hlcp_cmd_non_peers_t& cmd)
{
    non_peers_[cmd.param_].initialized = true;

    delete &cmd;
}

void hlcp_client_t::on_hlcp_sync(hlcp_cmd_sync_t& cmd)
{
    if (cmd.param_ == HCL_INVALID_RANK)  // server sync
    {
        cmd_sync_.completed_ = true;
    }
    else
    {
        VERIFY(!non_peers_[cmd.param_].synched, "non peer {} already synchronized", cmd.param_);

        non_peers_[cmd.param_].synched = true;
    }
}

void hlcp_client_t::on_hlcp_nic_state(hlcp_cmd_nic_state_t& cmd)
{
    migration_cb_->mcNicStateChange(cmd.param_);
}

bool hlcp_client_t::rendezvous()
{
    hlcp_cmd_sync_t cmd(rank_);

    cmd_sync_.completed_ = false;

    RET_ON_FALSE(send_to_srv(cmd));

    wait_condition(cmd_sync_.completed_, gcfg_.op_timeout, "sync between ranks");

    HLCP_INF("completed");

    return true;
}

bool hlcp_client_t::exchangeRankInfo(int nranks, const RankInfoHeader& myRankInfo, ranks_headers_t& ranksInfo)
{
    reset();

    cmd_comm_data_.payload_      = ranksInfo.data();
    cmd_comm_data_.payload_size_ = nranks * sizeof(RankInfoHeader);
    cmd_comm_data_.completed_    = false;

    hlcp_cmd_rank_data_t cmd({myRankInfo, srv_.local_addr.port(), (uint32_t)nranks, comm_id_});

    HLCP_INF("rank: {} hlcp_port: {} comm_size:{}",
             cmd.param_.info.hcclRank,
             cmd.param_.hlcp_port,
             cmd.param_.comm_size);

    RET_ON_FALSE(send_to_srv(cmd));

    wait_condition(cmd_comm_data_.completed_, gcfg_.op_timeout, "receive comm data");

    for (const auto& hdr : ranksInfo)
    {
        rank_addr_[hdr.hcclRank] = hdr.caddr;
    }

    HLCP_INF("completed");

    return true;
}

bool hlcp_client_t::xchg_qps_conf(int                   nranks,
                                  const RankInfoBuffer& myRankInfo,
                                  uint32_t              rankInfoBufferSize,
                                  remote_devices_t&     remoteDevicesInfo)
{
    HLCP_LOG("nranks={}, rankInfoBufferSize={}, remoteDevicesInfo.size={}",
             nranks,
             rankInfoBufferSize,
             remoteDevicesInfo.size());

    cmd_qps_conf_.payload_      = remoteDevicesInfo.data();
    cmd_qps_conf_.payload_size_ = nranks * sizeof(RemoteDeviceConnectionInfo);

    cmd_qps_conf_.completed_ = false;

    hlcp_cmd_qps_conf_t cmd(nranks, const_cast<void*>(reinterpret_cast<const void*>(&myRankInfo)), rankInfoBufferSize);

    RET_ON_FALSE(send_to_srv(cmd));

    wait_condition(cmd_qps_conf_.completed_, gcfg_.op_timeout, "recv QPs configuration");

    HLCP_INF("completed");

    return true;
}

bool hlcp_client_t::exchangeQpsInfo(int                   nranks,
                                    const RankInfoBuffer& myRankInfo,
                                    uint32_t              rankInfoBufferSize,
                                    remote_devices_t&     remoteDevicesInfo)
{
    return xchg_qps_conf(nranks, myRankInfo, rankInfoBufferSize, remoteDevicesInfo);
}

bool hlcp_client_t::exchangeMigrationData(int                   nranks,
                                          const RankInfoBuffer& myRankInfo,
                                          uint32_t              rankInfoBufferSize,
                                          remote_devices_t&     remoteDevicesInfo)
{
    // we are in migration. so need to clean local non_peer_ data for
    // SendRecvRemoteRanks to succeed after comm update
    non_peers_.clear();
    non_peers_.resize(ranks_);

    return xchg_qps_conf(nranks, myRankInfo, rankInfoBufferSize, remoteDevicesInfo);
}

bool hlcp_client_t::send_to_rank(HCL_Rank rank, const hlcp_command_t& cmd)
{
    const auto& rank_addr = rank_addr_[rank];

    socket_t socket;

    RET_ON_FALSE(socket.connect(rank_addr, gcfg_.op_timeout));

    hlcp_t hlcp(socket);

    RET_ON_FALSE(hlcp.send_command(cmd, gcfg_.op_timeout));

    HLCP_LOG("--> {}: {}", rank, cmd);

    hlcp.recv_ack();

    return true;
}

hcclResult_t hlcp_client_t::sendRecvFromRanks(UniqueSortedVector& nonPeerRemoteRanks,
                                              std::vector<void*>& recvBuffers,
                                              std::vector<void*>& sendBuffers,
                                              size_t              sendRecvBufSize)
{
    if (!xchg_non_peer_data(nonPeerRemoteRanks, recvBuffers, sendBuffers, sendRecvBufSize)) return hcclInternalError;

    return hcclSuccess;
}

bool hlcp_client_t::non_peer_data_ready(const UniqueSortedVector& nonPeerRemoteRanks, bool init)
{
    for (const auto& rank : nonPeerRemoteRanks)
    {
        if (!(init ? non_peers_[rank].initialized : non_peers_[rank].synched)) return false;
    }

    return true;
}

bool hlcp_client_t::xchg_non_peer_data(const UniqueSortedVector& nonPeerRemoteRanks,
                                       const std::vector<void*>& recvBuffers,
                                       const std::vector<void*>& sendBuffers,
                                       size_t                    sendRecvBufSize)
{
    HLCP_INF("ranks: {}, send/recv size: {}", nonPeerRemoteRanks.size(), sendRecvBufSize);
    uint32_t i = 0;
    for (const HCL_Rank remoteRank : nonPeerRemoteRanks)
    {
        hlcp_cmd_non_peers_t cmd(rank_, sendBuffers[i++], sendRecvBufSize);
        send_to_rank(remoteRank, cmd);
    }

    wait_condition(non_peer_data_ready(nonPeerRemoteRanks, true), gcfg_.op_timeout, "recv non peer data");

    i = 0;
    for (const HCL_Rank remoteRank : nonPeerRemoteRanks)
    {
        std::memcpy(recvBuffers[i++], &non_peers_[remoteRank].data, sendRecvBufSize);
    }

    HLCP_INF("competed");

    return true;
}

bool hlcp_client_t::rendezvous(const UniqueSortedVector& nonPeerRemoteRanks)
{
    return sync_non_peers(nonPeerRemoteRanks);
}

bool hlcp_client_t::sync_non_peers(const UniqueSortedVector& nonPeerRemoteRanks)
{
    hlcp_cmd_sync_t cmd(rank_);

    for (const HCL_Rank remoteRank : nonPeerRemoteRanks)
    {
        send_to_rank(remoteRank, cmd);
    }

    wait_condition(non_peer_data_ready(nonPeerRemoteRanks, false), gcfg_.op_timeout, "sync non peers");

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

bool hlcp_client_t::sendNicStateChange(const NicState& nicState)
{
    HLCP_LOG("nic: {}, state: {}", nicState.nic, nicState.state ? "up" : "down");

    hlcp_cmd_nic_state_t cmd(nicState);

    return send_to_srv(hlcp_cmd_nic_state_t(nicState));
}

bool hlcp_client_t::send_to_srv(const hlcp_command_t& cmd)
{
    socket_t socket;

    RET_ON_FALSE(socket.connect(hlcp_srv_, gcfg_.op_timeout));

    hlcp_t hlcp(socket);

    RET_ON_FALSE(hlcp.send_command(cmd, gcfg_.op_timeout));

    HLCP_LOG("[comm:{}] {}", comm_id_, cmd);

    hlcp.recv_ack();

    return true;
}

bool hlcp_client_t::send_log_msg(CollectiveLogMessage& msg)
{
    // take current time since epoch, in milliseconds
    const std::chrono::system_clock::time_point current = std::chrono::system_clock::now();

    const std::chrono::milliseconds ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(current.time_since_epoch());

    msg.timestamp = ms.count();

    hlcp_cmd_log_msg_t cmd(msg);

    return send_to_srv(cmd);
}

void hlcp_client_t::on_hlcp_log_msg(hlcp_cmd_log_msg_t& cmd)
{
    const CollectiveLogMessage& msg = cmd.param_;

    if (msg.customError)
    {
        HLCP_ERR("Coordinator server reported an error: {}", msg.errorString);
    }
}

void hlcp_client_t::on_command(hlcp_command_t& cmd, hlcp_t& connection)
{
    close_connection(connection);

    HLCP_LOG("{}", cmd);

    switch (cmd.id())
    {
        case HLCP_COMM_DATA:
            on_hlcp_comm_data(static_cast<hlcp_cmd_comm_data_t&>(cmd));
            break;

        case HLCP_QPS_CONF:
            on_hlcp_qps_conf(static_cast<hlcp_cmd_qps_conf_t&>(cmd));
            break;

        case HLCP_SYNC:
            on_hlcp_sync(static_cast<hlcp_cmd_sync_t&>(cmd));
            break;

        case HLCP_NON_PEERS:
            on_hlcp_non_peer_data(static_cast<hlcp_cmd_non_peers_t&>(cmd));
            break;

        case HLCP_NIC_STATE:
            on_hlcp_nic_state(static_cast<hlcp_cmd_nic_state_t&>(cmd));
            break;

        case HLCP_LOG_MSG:  // collective log
            on_hlcp_log_msg(static_cast<hlcp_cmd_log_msg_t&>(cmd));
            break;

        default:
            HLCP_ERR("Unknown command id: {}", cmd.id());
            break;
    }
}

void hlcp_client_t::on_message(const hlcp_message_t& msg, hlcp_t& connection)
{
    HLCP_LOG("{}", msg);

    switch (msg.id)
    {
        case HLCP_COMM_DATA:
            connection.receive_payload(cmd_comm_data_);
            break;

        case HLCP_QPS_CONF:
            connection.receive_payload(cmd_qps_conf_);
            break;

        case HLCP_SYNC:
        {
            hlcp_cmd_sync_t command(msg);
            on_command(command, connection);
            break;
        }

        case HLCP_NON_PEERS:
        {
            auto& command = *(new hlcp_cmd_non_peers_t(msg));

            HCL_Rank rank = command.param_;

            VERIFY(!non_peers_[rank].initialized, "non peer {} already initialized", rank);

            command.payload_      = &non_peers_[rank].data;
            command.payload_size_ = msg.payload_size;

            connection.receive_payload(command);
            break;
        }

        case HLCP_NIC_STATE:
        {
            hlcp_cmd_nic_state_t command(msg);
            on_command(command, connection);
            break;
        }

        case HLCP_LOG_MSG:  // collective log
        {
            hlcp_cmd_log_msg_t command(msg);
            on_command(command, connection);
            break;
        }

        default:
            HLCP_ERR("Unknown message id: {} from: {}", msg.id, connection->str());
            drop_connection(connection);
            break;
    }
}
