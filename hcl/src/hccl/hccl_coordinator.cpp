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

#include "hccl_coordinator.h"

#include <ext/alloc_traits.h>            // for __alloc_traits<>::value_type
#include <poll.h>                        // for pollfd, poll, POLLIN
#include <sys/socket.h>                  // for sockaddr, accept, getsockname
#include <unistd.h>                      // for close, read
#include <cerrno>                        // for errno
#include <cstring>                       // for memcpy
#include <sstream>                       // for basic_ostream::operator<<
#include <utility>                       // for pair
#include "hccl_internal_defs.h"          // for client_info_t, hccl_rank_dis...
#include "hcl_tcp_utils.h"               // for sendAllToSocket, createServe...
#include "hcl_utils.h"                   // for LOG_HCL_DEBUG, LOG_HCL_ERR
#include "network_utils.h"               // for address_to_string, recv_all
#include "hcl_log_manager.h"             // for LOG_DEBUG, LOG_ERR, LOG_TRACE

std::mutex hccl_coordinator::coord_create_mtx_;

std::unique_ptr<hccl_coordinator> hccl_coordinator::create(bool use_global_comm_ip)
{
    std::lock_guard<std::mutex> lock(hccl_coordinator::coord_create_mtx_);
    int                         hccl_port;
    std::string                 ip;
    if (use_global_comm_ip)
    {
        ip        = get_global_comm_ip();
        hccl_port = get_global_comm_port();
        // check if address is on this host
        VERIFY(ip_is_local(ip), "provided ip({}) is not on local host", ip);
    }
    else
    {
        auto tcp_if = detect_tcp_if();
        ip          = tcp_if.ip_addr;
        hccl_port   = 0;
    }

    sockaddr_t ipaddr(ip, hccl_port);

    VERIFY(ipaddr.str() != "", "invalid global comm id specified. {} {}", ip, hccl_port);


    int server_socket = createServerSocket(ipaddr);
    if (server_socket < 0)
    {
        LOG_CRITICAL(HCL, "Failed to create server socket on {}.", ipaddr.str());
        LOG_CRITICAL(HCL, "{}.", getListenPorts());
        VERIFY(false, "Creating server socket ({}) failed", ipaddr.str());
    }

    LOG_DEBUG(HCL_COORD, "socket_opened: {} @ {}", server_socket, ipaddr.str());

    internal_unique_id_t internal_id = {ipaddr, sizeof(internal_id.address)};

    return std::unique_ptr<hccl_coordinator>(new hccl_coordinator(server_socket, internal_id));
}

void hccl_coordinator::get_unique_id(hcclUniqueId& unique_id)
{
    VERIFY(sizeof(unique_id) == unique_id_buff_.size(), "Unexpected unique_id size={}", unique_id_buff_.size());
    std::memcpy(reinterpret_cast<void*>(&unique_id), unique_id_buff_.data(), unique_id_buff_.size());
}

hccl_coordinator::hccl_coordinator(int server_socket, internal_unique_id_t& internal_id_s_)
: server_socket_(server_socket),
  quit_requested_(false),
  hccl_comm_size_(HCCL_COMM_SIZE_UNASSIGNED),
  m_initialHandshakeDone(false)
{
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

int hccl_coordinator::internal_id()
{
    return internal_id_;
}

hccl_coordinator::~hccl_coordinator()
{
    quit_requested_ = true;
    if (accept_thread_.joinable())
    {
        accept_thread_.join();
    }
    if (listen_thread_.joinable())
    {
        listen_thread_.join();
    }
    LOG_DEBUG(HCL_COORD, "threads joined, closing sockets.");
    {
        std::lock_guard<std::mutex> lock(srv_socket_mtx_);
        close(server_socket_);
    }
    {
        std::lock_guard<std::mutex> lock(comm_sockets_mtx_);
        for (auto socket : comm_sockets_)
        {
            close(socket);
        }
    }
}

hcclResult_t hccl_coordinator::run()
{
    accept_thread_ = std::thread {[this] {
        LOG_HCL_DEBUG(HCL_COORD, "starting accept thread");
        while (!quit_requested_)
        {
            try_accept();
        }
    }};

    listen_thread_ = std::thread {[this] {
        LOG_HCL_DEBUG(HCL_COORD, "starting listen thread");
        while (!quit_requested_)
        {
            try_listen();
        }
    }};
    return hcclSuccess;
}

void hccl_coordinator::try_accept()
{
    LOG_HCL_TRACE(HCL_COORD, "accept mtx try acq");
    std::lock_guard<std::mutex> lock(srv_socket_mtx_);
    LOG_HCL_TRACE(HCL_COORD, "accept mtx acquired");

    int timeout = 500;  // 500ms

    pollfd poll_desc {};
    poll_desc.fd     = server_socket_;
    poll_desc.events = POLLIN;

    int poll_status = poll(&poll_desc, 1, timeout);
    VERIFY(poll_status >= 0, "Invalid poll_status={}", poll_status);
    if (poll_status == 0)
    {
        // Nothing to do...
        return;
    }

    sockaddr_storage  client_address {};
    socklen_t client_address_length = sizeof(client_address);
    int       new_socket            = -1;
    int       connectionTrials      = GCFG_HCCL_TRIALS.value();

    while (new_socket < 0)
    {
        new_socket = accept(server_socket_, (struct sockaddr*)&client_address, &client_address_length);
        if (new_socket < 0)
        {
            LOG_HCL_WARN(HCL_COORD, "msg from socket.accept: errno: {}", errno);
            connectionTrials--;

            if (connectionTrials == 0)
            {
                VERIFY(connectionTrials != 0, "Reached max attempts to accept a connection on a socket");
            }
        }
    }
    LOG_HCL_DEBUG(HCL_COORD, "accepted: {}", address_to_string(&client_address));

    deferred_launcher_.request_task([this, new_socket, client_address] {
        /* Non-blocking sockets require special care.
          int flags = fcntl(new_socket, F_GETFL, 0) | O_NONBLOCK;
          int status = fcntl(new_socket, F_SETFL, flags);
        */

        LOG_HCL_DEBUG(HCL_COORD, "adding new client socket to list.");
        {
            LOG_HCL_TRACE(HCL_COORD, "add mtx try acq");
            std::lock_guard<std::mutex> lock(comm_sockets_mtx_);

            LOG_HCL_TRACE(HCL_COORD, "add mtx acq");

            comm_sockets_.insert(new_socket);

            client_info_[new_socket].rank_info.hccl_rank = HCCL_RANK_UNASSIGNED;
            client_info_[new_socket].addr                = client_address;
        }
    });
}

void hccl_coordinator::try_listen()
{
    deferred_launcher_.assure_ready();
    LOG_HCL_TRACE(HCL_COORD, "try_listen mtx try acq");
    std::lock_guard<std::mutex> lock(comm_sockets_mtx_);
    LOG_HCL_TRACE(HCL_COORD, "try_listen mtx acq");
    std::vector<pollfd> sockets_to_listen;
    {
        for (int socket : comm_sockets_)
        {
            sockets_to_listen.push_back({socket, POLLIN, 0});
        }
    }

    if (sockets_to_listen.size() == 0)
    {
        LOG_HCL_TRACE(HCL_COORD, "Nothing to listen here");
        // Nothing to listen here
        return;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
    int status = poll(sockets_to_listen.data(), sockets_to_listen.size(), 500);

    if (status == 0)
    {
        LOG_HCL_TRACE(HCL_COORD, "No new message, returning...");
        // No new message, returning...
        return;
    }

    if (status <= 0)
    {
        LOG_HCL_DEBUG(HCL_COORD, " There are errors on the socket. Status: {}", status);
        return;
    }

    LOG_HCL_TRACE(HCL_COORD, "There is something to listen");

    for (pollfd& poll_desc : sockets_to_listen)
    {
        VERIFY(poll_desc.fd != 0, "Invalid poll_desc.fd");
        LOG_HCL_TRACE(HCL_COORD,
                      "Listening socket: {} events: {} revents: {}",
                      poll_desc.fd,
                      poll_desc.events,
                      poll_desc.revents);
        if (poll_desc.events == poll_desc.revents)
        {
            // Read the message header, containing the message's payload size.
            msg_header_t hdr {};
            int          recv_result = recv_all(poll_desc.fd, reinterpret_cast<void*>(&hdr), sizeof(hdr));
            if (recv_result <= 0)
            {
                // Ignore result of value 0, which is a result of a closed socket.
                if (recv_result < 0)
                {
                    LOG_HCL_ERR(HCL_COORD,
                                "Failed to read the message header from the client: socket error: {}",
                                recv_result);
                }
                // Skip to an another socket, ignoring the potential error.
                continue;
            }

            LOG_HCL_DEBUG(HCL_COORD, "Received {} hdr bytes from socket: {}", recv_result, poll_desc.fd);
            LOG_HCL_DEBUG(HCL_COORD, "{}", dump_header(hdr));

            auto payload_buff = std::vector<uint8_t>(hdr.payload_size);

            recv_result = recv_all(poll_desc.fd, reinterpret_cast<void*>(payload_buff.data()), payload_buff.size());
            if (recv_result <= 0)
            {
                LOG_HCL_ERR(HCL_COORD,
                            "Failed to read the message payload from the client: socket error: {}",
                            recv_result);
                // Skip to an another socket.
                continue;
            }
            LOG_HCL_DEBUG(HCL_COORD, "Received {} payload bytes from socket: {}", recv_result, poll_desc.fd);

            dispatch_msg(poll_desc.fd, hdr, payload_buff);
        }
    }
}

std::string hccl_coordinator::dump_header(msg_header_t& hdr)
{
    std::stringstream ss;
    ss << "Msg id: " << hdr.id << " seq: " << hdr.sequence << " payload bytes: " << hdr.payload_size;
    return ss.str();
}

void hccl_coordinator::dispatch_msg(int socket, msg_header_t& hdr, std::vector<uint8_t>& payload)
{
    switch (hdr.id)
    {
        case COMM_INIT_NEW_CONN:
        {
            processNewBootstrapConn(socket, *(reinterpret_cast<hcclBsCommInfo*>(payload.data())));
            break;
        }
        case COMM_INIT_HANDSHAKE1:
        {
            processCommInitHandshake1(socket, *(reinterpret_cast<RankInfoHeader*>(payload.data())));
            break;
        }
        case COMM_INIT_HANDSHAKE2:
        {
            processCommInitHandshake2(socket, payload);
            break;
        }
        case SYNC_BETWEEN_RANKS:
        {
            process_sync_between_ranks_msg(*(reinterpret_cast<hccl_bootstrap_general_payload_t*>(payload.data())));
            break;
        }
        case DATA_BETWEEN_RANKS:
        {
            process_ranks_exchange_msg(socket, hdr, payload);
            break;
        }
        case BOOTSTRAP_COMM_DESTROY:
        {
            process_comm_destroy_msg(*(reinterpret_cast<hccl_bootstrap_general_payload_t*>(payload.data())));
            break;
        }
        case COLLECTIVE_LOG:
        {
            processCollectiveLog(*(reinterpret_cast<CollectiveLogMessage*>(payload.data())));
            break;
        }
        default:
        {
            VERIFY(false, "Unknown header id={}", hdr.id);
            break;
        }
    }
}

void hccl_coordinator::process_sync_between_ranks_msg(hccl_bootstrap_general_payload_t& payload)
{
    if (sync_ranks_.find(payload.rank) != sync_ranks_.end())
    {
        VERIFY(false, "Rank {} was already finalized", payload.rank);
    }

    if (sync_ranks_.size() == 0)
    {
        // log first connection
        LOG_HCL_INFO(HCL_COORD, "Coordinator received sync request from 1'st rank({})", payload.rank);
    }

    sync_ranks_.insert(payload.rank);
    if (sync_ranks_.size() == (unsigned)hccl_comm_size_)
    {
        LOG_HCL_INFO(HCL_COORD,
                     "Coordinator received sync request from all ({}) ranks, sending ack to each",
                     hccl_comm_size_);
        sync_ranks_.clear();
        // clang-format off
        parallel_for_void(auto const &coord_sockets : rank_sockets, std::bind([&](auto &coord_sockets) {
            auto socket = coord_sockets.second.send_socket;
            bool sync_between_ranks_finished = true;
            LOG_HCL_TRACE(HCL_COORD, "Sending data to socket: {}", socket);
            if (!sendAllToSocket(socket, reinterpret_cast<const void*>(&sync_between_ranks_finished), sizeof(bool)))
            {
                LOG_HCL_ERR(HCL_COORD, "Socket={} send failed.", socket);
                return;
            }
            LOG_HCL_TRACE(HCL_COORD, "{} bytes sent.", sizeof(bool));
        }, coord_sockets));
        // clang-format on
        LOG_HCL_INFO(HCL_COORD, "Coordinator sync Done");
    }
}

bool hccl_coordinator::graceful_close_bootstrap_socket(int bootstrap_socket)
{
    LOG_HCL_DEBUG(HCL_COORD, "Shutting down socket={}", bootstrap_socket);
    shutdown(bootstrap_socket, SHUT_WR);

    LOG_HCL_DEBUG(HCL_COORD, "Empty read on socket={}", bootstrap_socket);
    int buffer;
    for (;;)
    {
        int res = read(bootstrap_socket, &buffer, sizeof(buffer));
        if (res < 0)
        {
            LOG_HCL_ERR(HCL_COORD, "Reading from socket={} ended with an error", bootstrap_socket);
            return false;
        }
        if (!res) break;
    }

    LOG_HCL_DEBUG(HCL_COORD, "Closing socket={}", bootstrap_socket);
    close(bootstrap_socket);

    return true;
}

void hccl_coordinator::process_comm_destroy_msg(hccl_bootstrap_general_payload_t& payload)
{
    if (sync_ranks_.find(payload.rank) != sync_ranks_.end())
    {
        VERIFY(false, "Rank {} was already finalized", payload.rank);
    }

    LOG_HCL_DEBUG(HCL_COORD, "received indication from rank={}", payload.rank);

    sync_ranks_.insert(payload.rank);
    if (sync_ranks_.size() == (unsigned)hccl_comm_size_)
    {
        sync_ranks_.clear();
        // clang-format off
        parallel_for_void(auto const &coord_sockets : rank_sockets, std::bind([&](auto &coord_sockets) {
            bool comm_init_rank_finished = true;
            auto socket = coord_sockets.second.send_socket;
            LOG_HCL_DEBUG(HCL_COORD, "Sending data to socket: {}", socket);
            if (!sendAllToSocket(socket, reinterpret_cast<const void*>(&comm_init_rank_finished), sizeof(bool)))
            {
                LOG_HCL_ERR(HCL_COORD, "Socket={} send failed.", socket);
                return;
            }

            if (!graceful_close_bootstrap_socket(coord_sockets.second.send_socket))
            {
                LOG_HCL_ERR(HCL_COORD, "Closing send_socket={} failed.", coord_sockets.second.send_socket);
                return;
            }

            if (!graceful_close_bootstrap_socket(coord_sockets.second.recv_socket))
            {
                LOG_HCL_ERR(HCL_COORD, "Closing recv_socket={} failed.", coord_sockets.second.recv_socket);
                return;
            }

            if (!graceful_close_bootstrap_socket(coord_sockets.second.log_socket))
            {
                LOG_HCL_ERR(HCL_COORD, "Closing log_socket={} failed.", coord_sockets.second.log_socket);
                return;
            }

            // comm_sockets_ is locked by mutex in try_listen()
            std::lock_guard<std::mutex> lock(comm_sockets_thread_mtx_);
            comm_sockets_.erase(coord_sockets.second.send_socket);
            comm_sockets_.erase(coord_sockets.second.recv_socket);
            comm_sockets_.erase(coord_sockets.second.log_socket);
        }, coord_sockets));
        // clang-format on

        LOG_HCL_INFO(HCL_COORD, "Closed all sockets connected to coordinator, closing coordinator");
        quit_requested_ = true;
    }
}

void hccl_coordinator::processNewBootstrapConn(int new_socket, hcclBsCommInfo& commInfo)
{
    if (hccl_comm_size_ == HCCL_COMM_SIZE_UNASSIGNED)
    {
        LOG_HCL_INFO(HCL_COORD, "Coordinator received first bootstrap request");
        hccl_comm_size_ = commInfo.nRanks;
        m_collectiveLogger.setCommSize(hccl_comm_size_);
        m_hcclRankInfoHeaders.resize(hccl_comm_size_);
        m_hcclRemoteDevices.resize(hccl_comm_size_);
        for (uint32_t rank = 0; rank < (unsigned)hccl_comm_size_; rank++)
        {
            m_hcclRemoteDevices[rank].resize(hccl_comm_size_);
        }
        LOG_HCL_DEBUG(HCL_COORD, "comm size is initialized to {}", hccl_comm_size_);
    }
    else
    {
        VERIFY(hccl_comm_size_ == commInfo.nRanks,
               "Received invalid nRanks={} from rank={} expected={}",
               commInfo.nRanks,
               commInfo.hcclRank,
               hccl_comm_size_);
    }

    if (commInfo.socketType == BS_SEND_SOCKET)
    {
        VERIFY(rank_sockets[commInfo.hcclRank].send_socket == 0,
               "Send socket for rank={} was already initialized",
               commInfo.hcclRank);
        rank_sockets[commInfo.hcclRank].send_socket = new_socket;
        LOG_HCL_DEBUG(HCL_COORD, "initialize send socket for rank={}", commInfo.hcclRank);
    }
    else if (commInfo.socketType == BS_RECV_SOCKET)
    {
        VERIFY(rank_sockets[commInfo.hcclRank].recv_socket == 0,
               "Receive socket for rank={} was already initialized",
               commInfo.hcclRank);
        rank_sockets[commInfo.hcclRank].recv_socket = new_socket;
        LOG_HCL_DEBUG(HCL_COORD, "initialize recv socket for rank={}", commInfo.hcclRank);
    }
    else if (commInfo.socketType == BS_LOG_SOCKET)
    {
        VERIFY(rank_sockets[commInfo.hcclRank].log_socket == 0,
               "Collective log socket for rank={} was already initialized",
               commInfo.hcclRank);
        rank_sockets[commInfo.hcclRank].log_socket = new_socket;
        LOG_HCL_DEBUG(HCL_COORD, "initialize collective log socket for rank={}", commInfo.hcclRank);
    }
    else
    {
        VERIFY(false, "received unexpected socket type={}", commInfo.socketType);
    }
}

void hccl_coordinator::processCommInitHandshake1(int socket, RankInfoHeader& payload)
{
    if (sync_ranks_.size() == (unsigned)hccl_comm_size_)
    {
        LOG_HCL_WARN(HCL_COORD, "Coordinator({}) received unexpected CommInitHandshake msg. Ignoring.", internal_id());
        return;
    }

    if (sync_ranks_.find(payload.hcclRank) != sync_ranks_.end())
    {
        VERIFY(false, "Rank {} was already finalized", payload.hcclRank);
    }

    if (sync_ranks_.size() == 0)
    {
        // log first connection
        LOG_HCL_INFO(HCL_COORD,
                     "Coordinator received handshake1 payload({}) from 1'st rank({})",
                     sizeof(payload),
                     payload.hcclRank);
    }

    sync_ranks_.insert(payload.hcclRank);

    // store headers to send to all ranks
    m_hcclRankInfoHeaders[payload.hcclRank] = payload;

    // Register ranks and their node's
    std::string ip_addr = address_to_string(&client_info_[socket].addr);
    if (m_nodeMapping.find(ip_addr) == m_nodeMapping.end())
    {
        m_nodeMapping[ip_addr] = 1;
    }
    else
    {
        m_nodeMapping[ip_addr]++;
    }

    LOG_HCL_TRACE(HCL_COORD, "Received init msg from rank={} on socket={}", payload.hcclRank, socket);
    if (sync_ranks_.size() == (unsigned)hccl_comm_size_)
    {
        LOG_HCL_INFO(HCL_COORD,
                     "Coordinator received handshake1 from all ({}) ranks, sending ({}) bytes to each",
                     hccl_comm_size_,
                     sizeof(RankInfoHeader) * hccl_comm_size_);
        sync_ranks_.clear();

        int boxSize            = m_nodeMapping.begin()->second;
        for (auto node : m_nodeMapping)
        {
            if (node.second != boxSize)
            {
                VERIFY(false, "Registered different amount of ranks from different boxes");
            }
        }

        // set the box size for all ranks
        for (RankInfoHeader& rankInfo : m_hcclRankInfoHeaders)
        {
            rankInfo.boxSize = boxSize;
        }

        LOG_HCL_DEBUG(HCL_COORD, "Validated box_size={} for all boxes", boxSize);

        // clang-format off
        parallel_for_void(auto const &coord_sockets : rank_sockets, std::bind([&](auto &coord_sockets) {
            auto socket = coord_sockets.second.send_socket;
            LOG_HCL_DEBUG(HCL_COORD, "Sending data to socket: {}", socket);
            size_t bytes_to_send = sizeof(RankInfoHeader) * hccl_comm_size_;
            if (!sendAllToSocket(socket, reinterpret_cast<const void*>(m_hcclRankInfoHeaders.data()), bytes_to_send))
            {
                LOG_HCL_ERR(HCL_COORD, "Socket={} send failed.", socket);
                return;
            }
            LOG_HCL_DEBUG(HCL_COORD, "{} bytes sent.", bytes_to_send);
        }, coord_sockets));
        // clang-format on
        m_hcclRankInfoHeaders.clear();
        m_hcclRankInfoHeaders.shrink_to_fit();
        LOG_HCL_INFO(HCL_COORD, "Coordinator handshake1 Done");
    }
}

void hccl_coordinator::processCommInitHandshake2(int socket, std::vector<uint8_t>& payload)
{
    // make sure received size is expected size
    const uint32_t remote_size = payload.size() - sizeof(LocalRankInfo);
    LOG_HCL_TRACE(HCL_COORD,
                  "Coordinator({}) received({}) payload expected({}={}*{}).",
                  internal_id(),
                  remote_size,
                  sizeof(RemoteInfo) * hccl_comm_size_,
                  sizeof(RemoteInfo),
                  hccl_comm_size_);
    VERIFY(remote_size == sizeof(RemoteInfo) * hccl_comm_size_);
    RankInfoBuffer& buffer = *(reinterpret_cast<RankInfoBuffer*>(payload.data()));

    if (sync_ranks_.size() == (unsigned)hccl_comm_size_)
    {
        LOG_HCL_WARN(HCL_COORD, "Coordinator({}) received unexpected CommInitHandshake msg. Ignoring.", internal_id());
        return;
    }

    if (sync_ranks_.find(buffer.localInfo.header.hcclRank) != sync_ranks_.end())
    {
        VERIFY(false, "Rank {} was already finalized", buffer.localInfo.header.hcclRank);
    }

    if (sync_ranks_.size() == 0)
    {
        // log first connection
        LOG_HCL_INFO(HCL_COORD,
                     "Coordinator received handshake2 payload({}) from 1'st rank({})",
                     payload.size(),
                     buffer.localInfo.header.hcclRank);
    }

    sync_ranks_.insert(buffer.localInfo.header.hcclRank);
    int remoteRank = buffer.localInfo.header.hcclRank;

    // fill remote devices info
    for (uint32_t rank = 0; rank < (unsigned)hccl_comm_size_; rank++)
    {
        m_hcclRemoteDevices[rank][remoteRank].header     = buffer.localInfo.header;
        m_hcclRemoteDevices[rank][remoteRank].device     = buffer.localInfo.device;
        m_hcclRemoteDevices[rank][remoteRank].remoteInfo = buffer.remoteInfo[rank];
    }

    LOG_HCL_TRACE(HCL_COORD, "Received init msg from rank={} on socket={}", buffer.localInfo.header.hcclRank, socket);
    if (sync_ranks_.size() == (unsigned)hccl_comm_size_)
    {
        LOG_HCL_INFO(HCL_COORD,
                     "Coordinator received handshake2 from all ({}) ranks, sending ({}) bytes to each",
                     hccl_comm_size_,
                     sizeof(RemoteDeviceConnectionInfo) * hccl_comm_size_);
        sync_ranks_.clear();

        // clang-format off
        parallel_for_void(auto const &coord_sockets : rank_sockets, std::bind([&](auto &coord_sockets) {
            auto socket = coord_sockets.second.send_socket;
            auto rank   = coord_sockets.first;
            size_t bytes_to_send = sizeof(RemoteDeviceConnectionInfo) * hccl_comm_size_;
            LOG_HCL_DEBUG(HCL_COORD, "Sending ({}) bytes data to rank({}) on socket({})", bytes_to_send, rank, socket);
            if (!sendAllToSocket(socket, reinterpret_cast<const void*>(m_hcclRemoteDevices[rank].data()), bytes_to_send))
            {
                LOG_HCL_ERR(HCL_COORD, "Socket={} hcclRemoteDevices send failed.", socket);
                return;
            }
            if (!sendAllToSocket(socket, &m_bootstrapValidationError, sizeof(m_bootstrapValidationError)))
            {
                LOG_HCL_ERR(HCL_COORD, "Socket={} bootstrapValidationError bit send failed.", socket);
                return;
            }
            LOG_HCL_TRACE(HCL_COORD, "{} bytes sent.", bytes_to_send + sizeof(m_bootstrapValidationError));
        }, coord_sockets));
        // clang-format on
        for (uint32_t rank = 0; rank < (unsigned)hccl_comm_size_; rank++)
        {
            m_hcclRemoteDevices[rank].clear();
            m_hcclRemoteDevices[rank].shrink_to_fit();
        }
        m_hcclRemoteDevices.clear();
        m_hcclRemoteDevices.shrink_to_fit();
        LOG_HCL_INFO(HCL_COORD, "Coordinator handshake2 Done");
    }
}

void hccl_coordinator::process_ranks_exchange_msg(int socket, msg_header_t& hdr, std::vector<uint8_t>& payload)
{
    int  recv_rank_socket = rank_sockets[hdr.dest_peer].recv_socket;
    bool ackToSrc         = false;

    LOG_HCL_DEBUG(HCL_COORD,
                  "Sending hdr to dest_rank={} from src_rank={}, sequence={}, payload_size={}, socket={}",
                  hdr.dest_peer,
                  hdr.source_peer,
                  hdr.sequence,
                  hdr.payload_size,
                  recv_rank_socket);
    if (!sendAllToSocket(recv_rank_socket, reinterpret_cast<const void*>(&hdr), sizeof(hdr)))
    {
        LOG_HCL_ERR(HCL_COORD, "Socket={} sending header failed.", socket);
        return;
    }

    if (sendAllToSocket(recv_rank_socket, reinterpret_cast<const void*>(payload.data()), payload.size()))
    {
        ackToSrc = true;
    }

    LOG_HCL_DEBUG(HCL_COORD, "Sending ack={} to rank={}, socket={}", ackToSrc, hdr.source_peer, socket);
    if (!sendAllToSocket(socket, reinterpret_cast<const void*>(&ackToSrc), sizeof(ackToSrc)))
    {
        LOG_HCL_ERR(HCL_COORD, "Socket={} sending ACK failed...", socket);
        return;
    }
}

/**
 * @brief process collective log message received on the log socket
 *
 * @param msg - collective log message
 */
void hccl_coordinator::processCollectiveLog(const CollectiveLogMessage& msg)
{
    if (msg.bootstrapValidationError)
    {
        processCollectiveLogErr(msg);
    }
    else
    {
        processCollectiveLogMsg(msg);
    }
}

void hccl_coordinator::processCollectiveLogMsg(const CollectiveLogMessage& msg)
{
    std::chrono::milliseconds             ms(msg.timestamp);
    std::chrono::system_clock::time_point from_ms(ms);

    LOG_DEBUG(HCL_COORD,
             "[{:%H:%M:%S}.{:>03}] Rank({}) called ({}, {}, {}, {}, {}, {})",
             from_ms, ms.count() % 1000000ull,
             msg.rank,
             msg.op,
             msg.params.count,
             msg.params.datatype,
             msg.params.reduceOp,
             msg.params.peer,
             msg.params.root);

    m_collectiveLogger.processLogMessage(msg);
}

void hccl_coordinator::processCollectiveLogErr(const CollectiveLogMessage& msg)
{
    LOG_HCL_CRITICAL(HCL_COORD, "rank {} reported validation failure", msg.rank);
    sync_ranks_.insert(msg.rank);
    m_bootstrapValidationError = true;
}
