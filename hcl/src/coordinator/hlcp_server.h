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
#include "infra/futex.h"

#include "coordinator_defs.h"
#include "coordinator.h"
#include "hlcp_commands.h"
#include "platform/gen2_arch_common/types.h"  // for MAX_NICS_GEN2ARCH

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

    lock_t lock_;

    uint32_t comm_size_ = 0;

    // State diagram for hlcp_server_t
    //
    // +-----------+       +---------+       +-------------+
    // | inactive  | ----> | active  | ----> | operational |
    // +-----------+       +---------+       +-------------+
    //                          ^                    |
    //                          |                    |
    //                          ----------------------
    //
    // State transitions:
    // inactive    ->  active:      When the handshake starts.
    // active      ->  operational: When the handshake completes successfully.
    // operational ->  active       When reinitializing the communicator (f.e. number of ports changed in-flight)
    //

    enum
    {
        inactive,     // first time only
        active,       // started handshake
        operational,  // handshake completed successfully

    } state_ = inactive;

    counter_t cnt_synched_ranks_ = 0;

    nodes_map_t                     nodes_;
    ranks_headers_t                 ranks_headers_;
    remote_devices_array_t          ranks_connections_;
    remote_devices_counters_cache_t ranks_counters_;

    CollectiveLogger collective_logger_;

    uint32_t comm_init(uint32_t comm_size);
    void     comm_reset();
    void     on_init(HCL_Comm comm, uint32_t comm_size);

    void on_hlcp_rank_data(hlcp_cmd_rank_data_t& cmd);
    void on_hlcp_qps_conf(hlcp_cmd_qps_conf_t& cmd);
    void on_hlcp_sync(hlcp_cmd_sync_t& cmd);
    void on_hlcp_log_msg(hlcp_cmd_log_msg_t& cmd);
    void on_hlcp_nic_state(hlcp_cmd_nic_state_t& cmd);
    void on_hlcp_counters(hlcp_cmd_counters_t& cmd);

    bool check_counters();
    void validate_comm_data();
    void report_comm_error(const std::string& err);

    bool send_to_rank(HCL_Rank rank, const hlcp_command_t& cmd);

    using sp_hlcp_cmd_t = std::shared_ptr<hlcp_command_t>;
    void send_cmd(uint32_t start_index, uint32_t count, sp_hlcp_cmd_t sp_cmd);
    void send_qps_data(uint32_t start_index, uint32_t count, sp_hlcp_cmd_t sp_cmd);
    void send_counters_data(uint32_t start_index, uint32_t count, sp_hlcp_cmd_t sp_cmd);

    using sender_func_t = void (hlcp_server_t::*)(uint32_t start_index, uint32_t count, sp_hlcp_cmd_t sp_cmd);
    void parallel_send_to_all(sp_hlcp_cmd_t sp_cmd, sender_func_t func = &hlcp_server_t::send_cmd);

    using rank_ports_t = std::array<counter_t, MAX_NICS_GEN2ARCH>;

    // failed_ports_[1] == 5 meaning: 5 ranks have reported logical port 1 failure
    rank_ports_t failed_ports_ = {};

    uint64_t update_port_state(HCL_Rank rank, uint32_t nic, bool up);

public:
    hlcp_server_t(const sockaddr_t& addr);

    virtual void on_command(hlcp_command_t& cmd, hlcp_t& connection) override;  // specific command
    virtual void on_message(const hlcp_message_t& msg, hlcp_t& connection) override;

    virtual ~hlcp_server_t() = default;
};
