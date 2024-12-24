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

#pragma once

#include <vector>
#include <unordered_map>
#include <mutex>

#include "coordinator_defs.h"
#include "coordinator.h"
#include "hlcp_commands.h"

using futex_t     = FutexLock;
using locker_t    = std::lock_guard<futex_t>;
using nodes_map_t = std::map<std::string, uint32_t>;

class hlcp_server_t
: public IHcclCoordinator
, public coordinator_t
{
private:
    struct
    {
        uint64_t io_threads   = 4;
        uint64_t op_timeout   = 120;
        uint32_t send_threads = 1;
    } gcfg_;

    futex_t lock_;

    uint32_t comm_size_ = 0;

    // State diagram for hlcp_server_t
    //
    //                         -----------------------
    //                         |                     |
    //                         v                     |
    // +-----------+       +---------+       +-------------+
    // | inactive  | ----> | active  | ----> | operational |
    // +-----------+       +---------+       +-------------+
    //                                           ^       |
    //                                           |       v
    //                                       +-------------+
    //                                       |  migration  |
    //                                       +-------------+
    //
    // State transitions:
    // inactive    ->  active:      When the handshake starts.
    // active      ->  operational: When the handshake completes successfully.
    // operational ->  migration:   When migration starts.
    // migration   ->  operational: When migration completes.
    // operational ->  active:      comm re-initialization

    enum
    {
        inactive,     // first time only
        active,       // started handshake
        operational,  // handshake completed successfully
        migration     // migration in progress
    } state_ = inactive;

    counter_t cnt_synched_ranks_ = 0;

    bool comm_error_ = false;

    nodes_map_t            nodes_;
    ranks_headers_t        ranks_headers_;
    remote_devices_array_t ranks_connections_;

    CollectiveLogger collective_logger_;

    uint32_t comm_init(uint32_t comm_size);
    void     comm_reset();
    void     on_init(uint32_t comm_size);

    void on_hlcp_rank_data(hlcp_cmd_rank_data_t& cmd);
    void on_hlcp_qps_conf(hlcp_cmd_qps_conf_t& cmd);
    void on_hlcp_sync(hlcp_cmd_sync_t& cmd);
    void on_hlcp_log_msg(hlcp_cmd_log_msg_t& cmd);
    void on_hlcp_nic_state(hlcp_cmd_nic_state_t& cmd);

    void validate_comm_data();

    bool send_to_rank(HCL_Rank rank, const hlcp_command_t& cmd);

    using sp_hlcp_cmd_t = std::shared_ptr<hlcp_command_t>;
    void send_cmd(uint32_t start_index, uint32_t count, sp_hlcp_cmd_t sp_cmd);
    void send_qps_data(uint32_t start_index, uint32_t count, sp_hlcp_cmd_t sp_cmd);

    using sender_func_t = void (hlcp_server_t::*)(uint32_t start_index, uint32_t count, sp_hlcp_cmd_t sp_cmd);
    void parallel_send_to_all(sender_func_t func, sp_hlcp_cmd_t sp_cmd);

public:
    hlcp_server_t(const sockaddr_t& addr);

    virtual void on_command(hlcp_command_t& cmd, hlcp_t& connection) override;  // specific command
    virtual void on_message(const hlcp_message_t& msg, hlcp_t& connection) override;

    virtual ~hlcp_server_t() = default;
};
