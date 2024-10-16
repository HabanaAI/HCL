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

#pragma once

#include <cstddef>  // for size_t
#include <atomic>   // for atomic
#include <cstdint>  // for uint8_t
#include <map>      // for map
#include <memory>   // for unique_ptr
#include <mutex>    // for mutex
#include <string>   // for string
#include <thread>   // for thread
#include <vector>   // for vector
#include <set>      // for set
#include <future>   // for future, async

#include "hccl_types.h"             // for hcclResult_t, hcclUniqueId
#include "deferred_launcher_job.h"  // for deferred_launcher_job
#include "hccl_internal_defs.h"     // for msg_header_t (ptr only)
#include "hcl_types.h"              // for RankInfo
#include "collective_logger.h"      // for CollectiveLogger
#include "hcl_sockaddr.h"

#include "../coordinator/coordinator_defs.h"  // IHcclCoordinator

struct coord_sockets
{
    int send_socket;
    int recv_socket;
    int log_socket;
};

class hccl_coordinator : public IHcclCoordinator
{
public:
    virtual ~hccl_coordinator() override;
    virtual hcclResult_t run() override;
    hccl_coordinator(sockaddr_t& addr);

private:
    std::string dump_header(msg_header_t& hdr);
    // This function meant to be called from coordinator_thread_;
    void try_accept();
    void try_listen();
    // This is meant to be called internally from listen_thread.
    void dispatch_msg(int socket, msg_header_t& hdr, std::vector<uint8_t>& payload);
    void processNewBootstrapConn(int new_socket, hcclBsCommInfo& commInfo);
    void processCommInitHandshake1(int socket, RankInfoHeader& payload);
    void processCommInitHandshake2(int socket, std::vector<uint8_t>& payload);
    void process_sync_between_ranks_msg(hccl_bootstrap_general_payload_t& payload);
    void process_comm_destroy_msg(hccl_bootstrap_general_payload_t& payload);
    void process_ranks_exchange_msg(int socket, msg_header_t& hdr, std::vector<uint8_t>& payload);
    void processCollectiveLog(const CollectiveLogMessage& msg);
    void processCollectiveLogMsg(const CollectiveLogMessage& msg);
    void processCollectiveLogErr(const CollectiveLogMessage& msg);
    bool graceful_close_bootstrap_socket(int bootstrap_socket);

    deferred_launcher_job        deferred_launcher_;
    std::mutex                   srv_socket_mtx_;
    int                          server_socket_;
    std::atomic<bool>            quit_requested_;
    std::mutex                   comm_sockets_mtx_;
    std::mutex                   comm_sockets_thread_mtx_;
    std::set<int>                comm_sockets_;
    std::map<int, client_info_t> client_info_;
    std::map<int, coord_sockets> rank_sockets;
    std::thread                  accept_thread_;
    std::thread                  listen_thread_;
    static const int             HCCL_COMM_SIZE_UNASSIGNED = -1;
    static const int             HCCL_RANK_UNASSIGNED      = -1;
    int                          hccl_comm_size_;
    bool                         m_initialHandshakeDone;

    std::vector<RankInfoHeader>                          m_hcclRankInfoHeaders;
    std::vector<std::vector<RemoteDeviceConnectionInfo>> m_hcclRemoteDevices;
    std::map<std::string, int>                           m_nodeMapping;
    std::set<int>                                        sync_ranks_;
    bool m_bootstrapValidationError = false;  // did any of the ranks fail, for any reason, during bootstrap

    CollectiveLogger m_collectiveLogger;
};

#define parallel_for_void(LOOP, LAMBDA)                                                                                \
    {                                                                                                                  \
        std::vector<std::future<void>> futures;                                                                        \
        for (LOOP)                                                                                                     \
        {                                                                                                              \
            futures.push_back(std::async(std::launch::async, LAMBDA));                                                 \
        }                                                                                                              \
                                                                                                                       \
        for (std::future<void> & f : futures)                                                                          \
        {                                                                                                              \
            f.get();                                                                                                   \
        }                                                                                                              \
    }
