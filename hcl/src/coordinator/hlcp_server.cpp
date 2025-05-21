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
#include "hlcp_inc.h"
#include "hcl_log_manager.h"  // for LOG_ERR, LogManager, LOG_CRI...

#define SRV_LOG HCL_COORD_LOG
#define SRV_ERR HCL_COORD_ERR
#define SRV_INF HCL_COORD_INF
#define SRV_CRT HCL_COORD_CRT
#define SRV_WRN HCL_COORD_WRN
#define SRV_DBG HCL_COORD_DBG

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

    SRV_INF("{} {}", this, srv_.local_addr.str());

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
    ranks_counters_.resize(comm_size);

    for (auto& refVec : ranks_connections_)
    {
        refVec.resize(comm_size);
    }

    for (auto& refVec : ranks_counters_)
    {
        refVec.resize(comm_size);
    }

    SRV_INF("comm group initialized. (ranks({}), sender threads({}))", comm_size, gcfg_.send_threads);

    return comm_size;
}

bool hlcp_server_t::send_to_rank(HCL_Rank rank, const hlcp_command_t& cmd)
{
    sockaddr_t rank_addr = ranks_headers_[rank].caddr;

    socket_t socket;

    RET_ON_FALSE(socket.connect(rank_addr, gcfg_.op_timeout));

    hlcp_t hlcp(socket);

    RET_ON_FALSE(hlcp.send_command(cmd, gcfg_.op_timeout));

    SRV_LOG("--> {}: {}", rank, cmd);

    hlcp.recv_ack();

    return true;
}

void hlcp_server_t::send_qps_data(uint32_t start_index, uint32_t count, sp_hlcp_cmd_t)
{
    SRV_LOG("start: {}. count: {}", start_index, count);

    hlcp_cmd_qps_conf_t cmd(comm_size_);

    while (count--)
    {
        cmd.payload_ =
            payload_t {ranks_connections_[start_index].data(), sizeof(RemoteDeviceConnectionInfo) * comm_size_};

        send_to_rank(start_index++, cmd);
    }
}

void hlcp_server_t::send_counters_data(uint32_t start_index, uint32_t count, sp_hlcp_cmd_t sp_cmd)
{
    const bool all_reached = (static_cast<hlcp_cmd_counters_t&>(*sp_cmd)).param_.all_reached;

    SRV_LOG("start_index: {}. count: {} all_reached: {}", start_index, count, all_reached);

    hlcp_cmd_counters_t cmd(all_reached);

    while (count--)
    {
        cmd.payload_ =
            payload_t {ranks_counters_[start_index].data(), sizeof(RemoteDeviceSyncCountersInfo) * comm_size_};
        send_to_rank(start_index++, cmd);
    }
}

void hlcp_server_t::send_cmd(uint32_t start_index, uint32_t count, sp_hlcp_cmd_t sp_cmd)
{
    hlcp_command_t& cmd = *sp_cmd;

    SRV_LOG("start: {}. count: {} cmd: {}", start_index, count, cmd);

    while (count--)
    {
        send_to_rank(start_index++, cmd);
    }
}

void hlcp_server_t::report_comm_error(const std::string& err)
{
    SRV_CRT("{}", err);

    sp_hlcp_cmd_t sp_cmd = std::make_shared<hlcp_cmd_log_msg_t>();

    (static_cast<hlcp_cmd_log_msg_t&>(*sp_cmd)).param_ = err;

    parallel_send_to_all(sp_cmd);
}

void hlcp_server_t::validate_comm_data()
{
    SRV_LOG("");

    auto boxSize = nodes_.begin()->second;

    for (const auto& node : nodes_)
    {
        if (node.second != boxSize)
        {
            std::string s =
                fmt::format(FMT_COMPILE("inconsistent box size detected. ip: {}, size: {}"), node.first, node.second);
            report_comm_error(s);
            return;
        }
    }

    const bool L3 = ranks_headers_[0].L3;

    // set the box size for all ranks and check configuration
    for (auto& rankInfo : ranks_headers_)
    {
        rankInfo.boxSize = boxSize;
        if (rankInfo.L3 != L3)
        {
            HCL_Rank    rank = rankInfo.hcclRank;
            std::string s =
                fmt::format(FMT_COMPILE("inconsistent network configuration detected. rank:{} L3(IP):{} != {}"),
                            rank,
                            rankInfo.L3,
                            L3);
            report_comm_error(s);
            return;
        }
    }

    SRV_INF("Validated configuration for all boxes. box_size={}, L3(IP): {}", boxSize, L3);
}

void hlcp_server_t::comm_reset()
{
    comm_size_ = 0;
    nodes_.clear();
    ranks_headers_.clear();
    ranks_connections_.clear();
    ranks_counters_.clear();
    std::fill(failed_ports_.begin(), failed_ports_.end(), 0);
}

void hlcp_server_t::on_init(HCL_Comm comm, uint32_t comm_size)
{
    locker_t locker(lock_);
    if (state_ != active)
    {
        comm_reset();
        comm_id_   = comm;
        comm_size_ = comm_init(comm_size);
        state_     = active;
    }
}

void hlcp_server_t::parallel_send_to_all(sp_hlcp_cmd_t sp_cmd, sender_func_t func)
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
        parallel_send_to_all(std::make_shared<hlcp_cmd_sync_t>(HCL_INVALID_RANK));

        if (cmd.param_.migration)
        {
            SRV_LOG("Migration (FO/B) finished");
        }
    }
}

void hlcp_server_t::on_hlcp_rank_data(hlcp_cmd_rank_data_t& cmd)
{
    sockaddr_t rank_addr = cmd.param_.info.caddr;

    rank_addr.port(cmd.param_.hlcp_port);

    ranks_headers_[cmd.param_.info.hcclRank] = cmd.param_.info;

    ranks_headers_[cmd.param_.info.hcclRank].caddr = rank_addr;

    // Register ranks and their node's
    std::string ip_addr = rank_addr.addr();

    lock_.lock();

    auto nodes = ++nodes_[ip_addr];

    lock_.unlock();

    nics_mask_t failed_port_mask = ranks_headers_[cmd.param_.info.hcclRank].failedScaleOutPortsMask;

    for (auto nic : failed_port_mask)
    {
        failed_ports_[nic]++;
    }

    uint64_t done = ++cnt_synched_ranks_;

    SRV_INF("rank:{} ({} of {}) node:[{}]={}", cmd.param_.info.hcclRank, done, comm_size_, rank_addr.str(), nodes);

    if (done == comm_size_)
    {
        validate_comm_data();

        cnt_synched_ranks_ = 0;
        parallel_send_to_all(std::make_shared<hlcp_cmd_comm_data_t>(HCL_INVALID_RANK,
                                                                    ranks_headers_.data(),
                                                                    sizeof(RankInfoHeader) * comm_size_));
    }
}

void hlcp_server_t::on_hlcp_qps_conf(hlcp_cmd_qps_conf_t& command)
{
    const uint32_t remote_size = command.payload_size() - sizeof(LocalRankInfo);

    VERIFY(remote_size == sizeof(RemoteInfo) * comm_size_);

    const RankInfoBuffer& buffer = *(RankInfoBuffer*)command.payload();

    const HCL_Rank remote_rank = buffer.localInfo.header.hcclRank;

    VERIFY(remote_rank < comm_size_, "rank_id({}) is out of range({})", remote_rank, comm_size_);

    // Fill remote devices info
    for (uint32_t rank = 0; rank < comm_size_; rank++)
    {
        ranks_connections_[rank][remote_rank].header     = buffer.localInfo.header;
        ranks_connections_[rank][remote_rank].device     = buffer.localInfo.device;
        ranks_connections_[rank][remote_rank].remoteInfo = buffer.remoteInfo[rank];
    }
    uint64_t done = ++cnt_synched_ranks_;
    SRV_INF("rank:{} ({} of {})", remote_rank, done, comm_size_);

    if (done == comm_size_)
    {
        cnt_synched_ranks_ = 0;
        parallel_send_to_all(nullptr, &hlcp_server_t::send_qps_data);
    }

    delete &command;
}

bool hlcp_server_t::check_counters()
{
    bool all_reached = true;
    for (uint32_t rank = 0; rank < comm_size_; rank++)
    {
        SRV_LOG("Calculating from rank {}, all_reached={}, "
                "ranks_counters_[][] header.myCountersReached={}, collectivesCounter(0x{:x})",
                rank,
                all_reached,
                ranks_counters_[rank][rank].header.myCountersReached,
                ranks_counters_[rank][rank].header.collectivesCounter);
        all_reached = all_reached && ranks_counters_[rank][rank].header.myCountersReached;
        for (uint32_t otherRank = 0; otherRank < comm_size_; otherRank++)
        {
            if (ranks_counters_[rank][otherRank].remoteInfo.counters.send > 0 ||
                ranks_counters_[rank][otherRank].remoteInfo.counters.recv > 0)
            {
                SRV_LOG("Will send S/R buffer of rank {}, send[{}]=(0x{:x}), recv=[{}]=(0x{:x})",
                        rank,
                        otherRank,
                        ranks_counters_[rank][otherRank].remoteInfo.counters.send,
                        otherRank,
                        ranks_counters_[rank][otherRank].remoteInfo.counters.recv);
            }
        }
    }
    return all_reached;
}

void hlcp_server_t::on_hlcp_counters(hlcp_cmd_counters_t& cmd)
{
    const uint32_t remote_size = cmd.payload_size() - sizeof(FtSyncCountersInfoHeader);
    VERIFY(remote_size == sizeof(FtSyncCountersRemoteInfo) * comm_size_);

    SRV_LOG("cmd: payload()={:p}, payload_size()={}, remote_size={}", cmd.payload(), cmd.payload_size(), remote_size);

    if (unlikely(LOG_LEVEL_AT_LEAST_TRACE(HCL)))
    {
        SRV_LOG("cmd.id={}", cmd.id());
        const uint8_t* payload_array = reinterpret_cast<const uint8_t*>(cmd.payload());
        for (unsigned i = 0; i < cmd.payload_size(); i++)
        {
            if (payload_array[i] > 0)
            {
                SRV_LOG("cmd payload_array[{}]=(0x{:x})", i, payload_array[i]);
            }
        }
    }

    const FtRanksInfoBuffer& buffer = *(reinterpret_cast<const FtRanksInfoBuffer*>(cmd.payload()));
    SRV_LOG("buffer={:p}", &buffer);
    const HCL_Rank send_rank = buffer.localInfo.hcclRank;

    VERIFY(send_rank < comm_size_, "rank_id({}) is out of range({})", send_rank, comm_size_);

    const bool all_ranks_reported =
        ((++cnt_synched_ranks_) == comm_size_);  // If we got all ranks reporting their FT status

    SRV_LOG("rank: {} FT sync info {} of {}, myCountersReached={}, myCountersVersion={}, "
            "all_ranks_reported={}",
            send_rank,
            cnt_synched_ranks_,
            comm_size_,
            buffer.localInfo.myCountersReached,
            buffer.localInfo.myCountersVersion,
            all_ranks_reported);

    // Cache in remote devices info
    for (uint32_t rank = 0; rank < comm_size_; rank++)
    {
        ranks_counters_[rank][send_rank].header     = buffer.localInfo;
        ranks_counters_[rank][send_rank].remoteInfo = buffer.remoteInfo[rank];
        if (unlikely(LOG_LEVEL_AT_LEAST_TRACE(HCL)))
        {
            if (buffer.remoteInfo[rank].counters.send > 0 || buffer.remoteInfo[rank].counters.recv > 0)
            {
                SRV_LOG("Received S/R buffer of rank {}, send[{}]=(0x{:x}), recv=[{}]=(0x{:x})",
                        rank,
                        send_rank,
                        buffer.remoteInfo[rank].counters.send,
                        send_rank,
                        buffer.remoteInfo[rank].counters.recv);
            }
        }
    }
    SRV_LOG("Received from rank {}, collectivesCounter=(0x{:x})",
            send_rank,
            ranks_counters_[send_rank][send_rank].header.collectivesCounter);

    // If we received FT reached status from  all ranks, then calculate response and send back to all ranks
    if (all_ranks_reported)
    {
        const bool all_reached = check_counters();
        SRV_LOG("After update from rank {}, all_reached={}", send_rank, all_reached);
        cnt_synched_ranks_ = 0;  // Clear for next update from all ranks
        parallel_send_to_all(std::make_shared<hlcp_cmd_counters_t>(all_reached), &hlcp_server_t::send_counters_data);
    }

    delete &cmd;
}

void hlcp_server_t::on_hlcp_log_msg(hlcp_cmd_log_msg_t& cmd)
{
    const CollectiveLogMessage& msg = cmd.param_;

    std::chrono::milliseconds             ms(msg.timestamp);
    std::chrono::system_clock::time_point from_ms(ms);

    SRV_LOG("[{:%H:%M:%S}.{:>03}] Rank({}) called ({}, {}, {}, {}, {}, {})",
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

#define set_bit(value, nbit, on) ((on) ? ((value) |= (1ULL << (nbit))) : ((value) &= ~(1ULL << (nbit))))

// this function returns zero only in 2 cases:
//  1. all failed nics on all ranks are up => --failed_ports_[nic] == 0
//  2. first rank with failed nic          => failed_ports_[nic]++ == 0
uint64_t hlcp_server_t::update_port_state(HCL_Rank rank, uint32_t nic, bool up)
{
    set_bit(ranks_headers_[rank].failedScaleOutPortsMask, nic, !up);
    return up ? --failed_ports_[nic] : failed_ports_[nic]++;
}

void hlcp_server_t::on_hlcp_nic_state(hlcp_cmd_nic_state_t& cmd)
{
    if (update_port_state(cmd.param_.rank, cmd.param_.nic, cmd.param_.state) == 0)
    {
        // no ranks with failed nic => all is up OR first rank with failed nic
        SRV_LOG("{} started", cmd.param_.state ? "FailBack" : "FailOver");
        parallel_send_to_all(std::make_shared<hlcp_cmd_nic_state_t>(cmd));
    }
}

void hlcp_server_t::on_command(hlcp_command_t& cmd, hlcp_t& connection)
{
    SRV_LOG("{}", cmd);

    close_connection(connection);

    switch (cmd.id())
    {
        HLCP_CMD_HANDLER(HLCP_RANK_DATA, hlcp_cmd_rank_data_t, on_hlcp_rank_data);
        HLCP_CMD_HANDLER(HLCP_NIC_STATE, hlcp_cmd_nic_state_t, on_hlcp_nic_state);
        HLCP_CMD_HANDLER(HLCP_QPS_CONF, hlcp_cmd_qps_conf_t, on_hlcp_qps_conf);
        HLCP_CMD_HANDLER(HLCP_LOG_MSG, hlcp_cmd_log_msg_t, on_hlcp_log_msg);
        HLCP_CMD_HANDLER(HLCP_SYNC, hlcp_cmd_sync_t, on_hlcp_sync);
        HLCP_CMD_HANDLER(HLCP_COUNTERS_DATA, hlcp_cmd_counters_t, on_hlcp_counters);
    }
}

void hlcp_server_t::on_message(const hlcp_message_t& msg, hlcp_t& connection)
{
    SRV_LOG("{}", msg);

    if ((state_ != active) && (msg.id == HLCP_RANK_DATA))  // "first handshake"
    {
        const auto& param = *(hlcp_rank_data_param_t*)msg.param;
        on_init(param.comm, param.comm_size);
    }

    switch (msg.id)
    {
        HLCP_MSG_HANDLER(HLCP_RANK_DATA, hlcp_cmd_rank_data_t);
        HLCP_MSG_HANDLER(HLCP_SYNC, hlcp_cmd_sync_t);
        HLCP_MSG_HANDLER(HLCP_LOG_MSG, hlcp_cmd_log_msg_t);
        HLCP_MSG_HANDLER(HLCP_NIC_STATE, hlcp_cmd_nic_state_t);

        HLCP_MSG_PAYLOAD_HANDLER(HLCP_QPS_CONF, hlcp_cmd_qps_conf_t);
        HLCP_MSG_PAYLOAD_HANDLER(HLCP_COUNTERS_DATA, hlcp_cmd_counters_t);

        default:
            SRV_ERR("unknown msg id:{} remote:{} ", msg.id, connection->remote_addr.str());
            drop_connection(connection);
            break;
    }
}
