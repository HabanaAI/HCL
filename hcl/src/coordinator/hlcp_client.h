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

#include "coordinator_defs.h"
#include "hlcp_inc.h"
#include "hlcp_commands.h"
#include "coordinator.h"

using ranks_addrs_t   = std::vector<sockaddr_t>;
using addr_rank_map_t = std::unordered_map<std::string, HCL_Rank>;
using rank_infos_t    = std::vector<RankInfoHeader>;

class hlcp_client_t
: public IHcclCoordinatorClient
, public coordinator_t
{
public:  // IHcclCoordinatorClient
    hlcp_client_t(uint32_t nranks, HCL_Rank rank, const internal_unique_id_t* internalUniqueId);

    virtual bool destroy() override;

    virtual bool commInitHandshake1(int nranks, RankInfoHeader& myRankInfo, rank_infos_t& ranksInfo) override;

    virtual bool commInitHandshake2(int               nranks,
                                    void*             rankInfoBuffer,
                                    uint32_t          rankInfoBufferSize,
                                    remote_devices_t& remoteDevicesInfo) override;

    virtual bool syncBetweenRanks() override;

    virtual hcclResult_t sendCollectiveLog(const HCL_CollectiveOp op,
                                           const size_t           count,
                                           const hcclDataType_t   datatype,
                                           const hcclRedOp_t      reduceOp,
                                           const HCL_Rank         peer,
                                           const HCL_Rank         root) override;
    virtual hcclResult_t sendCollectiveLogErr() override;

    virtual hcclResult_t sendRecvFromRanks(UniqueSortedVector& nonPeerRemoteRanks,
                                           std::vector<void*>& recvBuffers,
                                           std::vector<void*>& sendBuffers,
                                           size_t              sendRecvBufSize,
                                           HCL_Comm            comm) override;

    virtual void synchronizeRemoteRanks(const HCL_Comm comm, const UniqueSortedVector& remoteRanks) override;

public:                                                                               // coordinator_t
    virtual void on_command(hlcp_command_t& cmd, hlcp_t& connection) override;        // specific command
    virtual void on_message(const hlcp_message_t& msg, hlcp_t& connection) override;  // no payload
    virtual void on_error(bool send, hlcp_command_t* cmd, const hlcp_packet_t& packet, hlcp_t& connection) override;
    virtual void on_connect(hlcp_t& connection) override;

private:
    bool sync_non_peers(const HCL_Comm comm, const UniqueSortedVector& remoteRanks);
    bool xchg_non_peer_data(const UniqueSortedVector& nonPeerRemoteRanks,
                            const std::vector<void*>& recvBuffers,
                            const std::vector<void*>& sendBuffers,
                            size_t                    sendRecvBufSize,
                            HCL_Comm                  comm);

    bool non_peer_data_ready(const UniqueSortedVector& nonPeerRemoteRanks, bool init);

    bool send_to_rank(HCL_Rank rank, const hlcp_command_t& cmd);
    bool send_to_srv(const hlcp_command_t& cmd);
    bool send_log_msg(CollectiveLogMessage& msg);

    HCL_Rank rank_  = HCL_INVALID_RANK;
    uint32_t ranks_ = 0;

    sockaddr_t hlcp_srv_;

private:
    struct remote_device_conn_info_t
    {
        bool initialized = false;
        bool synched     = false;
        union _remote_device_conn_info_t
        {
            RemoteDeviceConnectionInfo rd = {0};
            HostNicConnectInfo         hn;

            _remote_device_conn_info_t() {};
        } data;
    };

    using devices_conn_info_t = std::vector<remote_device_conn_info_t>;

    enum
    {
        uninitialized,
        comm_data,
        qps_conf,
        conf_done
    } state_ = uninitialized;

    struct
    {
        uint64_t io_threads = 2;
        uint64_t op_timeout = 120;  // sec
    } gcfg_;

    // commands we will receive in our srv socket
    hlcp_cmd_comm_data_t cmd_comm_data_;
    hlcp_cmd_qps_conf_t  cmd_qps_conf_;
    hlcp_cmd_sync_t      cmd_sync_;

    devices_conn_info_t non_peers_;
    addr_rank_map_t     addr_rank_;
    ranks_addrs_t       rank_addr_;
};
