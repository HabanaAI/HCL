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

#include "hlcp_server.h"

hlcp_server_t::hlcp_server_t(const sockaddr_t& ipaddr)
{
    gcfg_.io_threads = GCFG_HCL_HLCP_SERVER_IO_THREADS.value();
    gcfg_.op_timeout = GCFG_HCL_HLCP_OPS_TIMEOUT.value();

    if (!start(gcfg_.io_threads, ipaddr))
    {
        LOG_HCL_CRITICAL(HCL,
                         "Failed to create coordinator server on {}. ({}: {})",
                         ipaddr.str(),
                         errno,
                         strerror(errno));
        VERIFY(false, "Creating coordinator server ({}) failed", ipaddr.str());
    }

    HLCP_INF("{} {}", this, srv_.local_addr.str());

    internal_unique_id_t internal_id_s_ = {srv_.local_addr, sizeof(internal_id_s_.address)};

    hcclUniqueId unique_id;
    internal_id_s_.id = next_id();
    internal_id_      = internal_id_s_.id;

    VERIFY(sizeof(unique_id.internal) > sizeof(internal_unique_id_t),
           "Unexpected unique_id.internal size={}",
           sizeof(unique_id.internal));
    memcpy(unique_id.internal, (uint8_t*)&internal_id_s_, sizeof(internal_id_s_));
    unique_id.length = sizeof(internal_unique_id_t);

    unique_id_buff_.resize(sizeof(unique_id));

    memcpy(unique_id_buff_.data(), (uint8_t*)&unique_id, sizeof(unique_id));
}

uint32_t hlcp_server_t::comm_init(uint32_t comm_size)
{
    VERIFY(comm_size != 0, "zero comm group size specified");

    gcfg_.send_threads = ceil((float)comm_size / (float)GCFG_HCL_HLCP_SERVER_SEND_THREAD_RANKS.value());

    collective_logger_.setCommSize(comm_size);

    ranks_headers_.resize(comm_size);

    ranks_connections_.resize(comm_size);

    for (auto& refVec : ranks_connections_)
    {
        refVec.resize(comm_size);
    }

    HLCP_INF("comm group initialized. (ranks({}), sender threads({}))", comm_size, gcfg_.send_threads);

    return comm_size;
}

bool hlcp_server_t::send_to_rank(HCL_Rank rank, const hlcp_command_t& cmd)
{
    sockaddr_t rank_addr = ranks_headers_[rank].caddr;

    socket_t socket;

    RET_ON_FALSE(socket.connect(rank_addr, gcfg_.op_timeout));

    hlcp_t hlcp(socket);

    RET_ON_FALSE(hlcp.send_command(cmd, gcfg_.op_timeout));

    HLCP_LOG("--> {}: {}", rank, cmd);

    hlcp.recv_ack();

    return true;
}

void hlcp_server_t::send_qps_data(uint32_t start_index, uint32_t count, sp_hlcp_cmd_t sp_cmd)
{
    HLCP_LOG("start: {}. count: {}", start_index, count);

    hlcp_cmd_qps_conf_t cmd(comm_size_);

    while (count--)
    {
        cmd.payload_      = ranks_connections_[start_index].data();
        cmd.payload_size_ = sizeof(RemoteDeviceConnectionInfo) * comm_size_;

        send_to_rank(start_index++, cmd);
    }
}

void hlcp_server_t::send_cmd(uint32_t start_index, uint32_t count, sp_hlcp_cmd_t sp_cmd)
{
    hlcp_command_t& cmd = *sp_cmd;

    HLCP_LOG("start: {}. count: {} cmd: {}", start_index, count, cmd);

    while (count--)
    {
        send_to_rank(start_index++, cmd);
    }
}

void hlcp_server_t::validate_comm_data()
{
    HLCP_LOG("");

    auto boxSize = nodes_.begin()->second;

    for (const auto& node : nodes_)
    {
        if (node.second != boxSize)
        {
            VERIFY(false, "Registered different amount of ranks from different boxes");
        }
    }

    const bool L3 = ranks_headers_[0].L3;

    // set the box size for all ranks and check configuration
    for (RankInfoHeader& rankInfo : ranks_headers_)
    {
        rankInfo.boxSize = boxSize;
        VERIFY(rankInfo.L3 == L3, "rank:{} L3:{} != {}", rankInfo.hcclRank, rankInfo.L3, L3);
    }

    HLCP_LOG("Validated configuration for all boxes. box_size={}, L3(IP): {}", boxSize, L3);
}

void hlcp_server_t::comm_reset()
{
    comm_size_  = 0;
    comm_error_ = false;
    nodes_.clear();
    ranks_headers_.clear();
    ranks_connections_.clear();
}

void hlcp_server_t::on_init(uint32_t comm_size)
{
    locker_t locker(lock_);
    if (state_ != active)
    {
        comm_reset();
        comm_size_ = comm_init(comm_size);
        state_     = active;
    }
}

void hlcp_server_t::parallel_send_to_all(sender_func_t func, sp_hlcp_cmd_t sp_cmd)
{
    uint32_t base      = comm_size_ / gcfg_.send_threads;
    uint32_t remainder = comm_size_ % gcfg_.send_threads;

    uint32_t start_index = 0;
    FOR_I(gcfg_.send_threads)
    {
        uint32_t ranks_in_thread = i < remainder ? base + 1 : base;

        if (ranks_in_thread) std::thread(func, this, start_index, ranks_in_thread, sp_cmd).detach();

        start_index += ranks_in_thread;
    }
}

void hlcp_server_t::on_hlcp_sync(hlcp_cmd_sync_t& cmd)
{
    if (++cnt_synched_ranks_ == comm_size_)
    {
        state_             = operational;
        cnt_synched_ranks_ = 0;
        parallel_send_to_all(&hlcp_server_t::send_cmd, std::make_shared<hlcp_cmd_sync_t>(HCL_INVALID_RANK));
    }
}

void hlcp_server_t::on_hlcp_rank_data(hlcp_cmd_rank_data_t& cmd)
{
    sockaddr_t rank_addr = cmd.param_.info.caddr;

    rank_addr.port(cmd.param_.hlcp_port);

    ranks_headers_[cmd.param_.info.hcclRank] = cmd.param_.info;

    ranks_headers_[cmd.param_.info.hcclRank].caddr = rank_addr;

    HLCP_LOG("rank:{} addr:{}", cmd.param_.info.hcclRank, rank_addr.str());

    // Register ranks and their node's
    std::string ip_addr = rank_addr.addr();

    lock_.lock();

    nodes_[ip_addr]++;

    HLCP_LOG("{} rank:{} node[{}]={}", this, cmd.param_.info.hcclRank, ip_addr, nodes_[ip_addr]);

    lock_.unlock();

    uint64_t done = ++cnt_synched_ranks_;

    HLCP_LOG("initialized {} of {}", done, comm_size_);

    if (done == comm_size_)
    {
        cnt_synched_ranks_ = 0;

        validate_comm_data();

        sp_hlcp_cmd_t sp_cmd = std::make_shared<hlcp_cmd_comm_data_t>(HCL_INVALID_RANK,
                                                                      ranks_headers_.data(),
                                                                      sizeof(RankInfoHeader) * comm_size_);
        parallel_send_to_all(&hlcp_server_t::send_cmd, sp_cmd);
    }
}

void hlcp_server_t::on_hlcp_qps_conf(hlcp_cmd_qps_conf_t& command)
{
    const uint32_t remote_size = command.payload_size() - sizeof(LocalRankInfo);

    VERIFY(remote_size == sizeof(RemoteInfo) * comm_size_);

    RankInfoBuffer& buffer = *(RankInfoBuffer*)command.payload();

    HCL_Rank remoteRank = buffer.localInfo.header.hcclRank;

    HLCP_LOG("rank: {}", remoteRank);

    // fill remote devices info
    for (uint32_t rank = 0; rank < comm_size_; rank++)
    {
        ranks_connections_[rank][remoteRank].header     = buffer.localInfo.header;
        ranks_connections_[rank][remoteRank].device     = buffer.localInfo.device;
        ranks_connections_[rank][remoteRank].remoteInfo = buffer.remoteInfo[rank];
    }

    if (++cnt_synched_ranks_ == comm_size_)
    {
        cnt_synched_ranks_ = 0;
        parallel_send_to_all(&hlcp_server_t::send_qps_data, nullptr);
    }

    command.free_payload();
    delete &command;
}

void hlcp_server_t::on_hlcp_log_msg(hlcp_cmd_log_msg_t& cmd)
{
    const CollectiveLogMessage& msg = cmd.param_;

    if (msg.bootstrapValidationError)
    {
        HLCP_CRT("rank {} reported validation failure", msg.rank);
        comm_error_ = true;
    }
    else
    {
        std::chrono::milliseconds             ms(msg.timestamp);
        std::chrono::system_clock::time_point from_ms(ms);

        HLCP_DBG("[{:%H:%M:%S}.{:>03}] Rank({}) called ({}, {}, {}, {}, {}, {})",
                 from_ms,
                 ms.count() % 1000000ull,
                 msg.rank,
                 msg.op,
                 msg.params.count,
                 msg.params.datatype,
                 msg.params.reduceOp,
                 msg.params.peer,
                 msg.params.root);

        collective_logger_.processLogMessage(msg);
    }
}

void hlcp_server_t::on_hlcp_nic_state(hlcp_cmd_nic_state_t& cmd)
{
    bool send = false;
    if (state_ != migration)
    {
        locker_t locker(lock_);
        if (state_ != migration)
        {
            state_ = migration;
            send   = true;
        }
    }

    if (send)
    {
        cnt_synched_ranks_ = 0;
        parallel_send_to_all(&hlcp_server_t::send_cmd, std::make_shared<hlcp_cmd_nic_state_t>(cmd));
    }
}

void hlcp_server_t::on_command(hlcp_command_t& cmd, hlcp_t& connection)
{
    HLCP_LOG("cmd: {}", cmd);

    close_connection(connection);

    switch (cmd.id())
    {
        case HLCP_RANK_DATA:  // "first handshake"
            on_hlcp_rank_data(static_cast<hlcp_cmd_rank_data_t&>(cmd));
            break;

        case HLCP_QPS_CONF:  // "second handshake" and migration
            on_hlcp_qps_conf(static_cast<hlcp_cmd_qps_conf_t&>(cmd));
            break;

        case HLCP_SYNC:  // sync message
            on_hlcp_sync(static_cast<hlcp_cmd_sync_t&>(cmd));
            break;

        case HLCP_LOG_MSG:  // collective log
            on_hlcp_log_msg(static_cast<hlcp_cmd_log_msg_t&>(cmd));
            break;

        case HLCP_NIC_STATE:  // nic down/up
            on_hlcp_nic_state(static_cast<hlcp_cmd_nic_state_t&>(cmd));
            break;
    }
}

void hlcp_server_t::on_message(const hlcp_message_t& msg, hlcp_t& connection)
{
    HLCP_LOG("msg: {}", msg);

    switch (msg.id)
    {
        case HLCP_RANK_DATA:  // "first handshake"
        {
            if (state_ != active)
            {
                hlcp_rank_data_param_t& param = *(hlcp_rank_data_param_t*)msg.param;
                on_init(param.comm_size);
            }

            hlcp_cmd_rank_data_t command(msg);
            command.param_.info.caddr = connection->remote_addr;
            on_command(command, connection);
            break;
        }

        case HLCP_QPS_CONF:  // "second handshake" and migration
        {
            hlcp_cmd_qps_conf_t& command = *(new hlcp_cmd_qps_conf_t(msg));
            command.alloc_payload();
            connection.receive_payload(command);  // will call on_command() after payload received
            break;
        }

        case HLCP_SYNC:  // sync message
        {
            hlcp_cmd_sync_t command(msg);
            on_command(command, connection);
            break;
        }

        case HLCP_LOG_MSG:  // collective log
        {
            hlcp_cmd_log_msg_t command(msg);
            on_command(command, connection);
            break;
        }

        case HLCP_NIC_STATE:  // nic down/up
        {
            hlcp_cmd_nic_state_t command(msg);
            on_command(command, connection);
            break;
        }

        default:
            HLCP_ERR("unknown msg id:{} remote:{} ", msg.id, connection->remote_addr.str());
            drop_connection(connection);
            break;
    }
}
