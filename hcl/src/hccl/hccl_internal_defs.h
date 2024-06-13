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

#include <sys/types.h>
#include <sys/socket.h>
#include <atomic>                        // for atomic
#include <bits/stdc++.h>                 // for INT_MAX
#include "hccl_types.h"                  // for hcclResult_t, hcclComm_t, hcc...
#include <hcl_api_types.h>               // for HCL_UniqueId
#include "hcl_utils.h"                   // for LOG_HCL_

class ofi_req_t;
struct ofiComm_t;

#define CORD_ID_GLOBAL_COMM 1
constexpr int HOST_BUFF_INC = 64 * 1024 * 1024;  // 64MB
struct internal_unique_id_t
{
    sockaddr_storage address;
    socklen_t        length;
    size_t           id;
};

typedef enum
{
    COMM_INIT_NEW_CONN = 1,
    COMM_INIT_HANDSHAKE1,       // handshake phase 1
    COMM_INIT_HANDSHAKE2,       // handshake phase 2
    SYNC_BETWEEN_RANKS,
    DATA_BETWEEN_RANKS,
    BOOTSTRAP_COMM_DESTROY,
    COLLECTIVE_LOG,             // log over bootstrap network
} bootstrap_hdr_id_t;

struct msg_header_t
{
    bootstrap_hdr_id_t id;
    uint32_t           sequence;
    uint32_t           payload_size;
    int                source_peer;
    int                dest_peer;
};

#define COMM_INIT_MSG_HEADER_SIZE (sizeof(msg_header));

typedef enum bootstrapSocketType
{
    NO_TYPE = 0,
    BS_SEND_SOCKET,
    BS_RECV_SOCKET,
    BS_LOG_SOCKET,
} bootstrapSocketType;

struct hcclBsCommInfo
{
    int                 nRanks;
    bootstrapSocketType socketType;
    int                 hcclRank;
};

struct comm_init_rank_info_t
{
    int hccl_rank;
    int host_id;
};

struct client_info_t
{
    sockaddr_storage      addr;
    comm_init_rank_info_t rank_info;
};

struct hccl_rank_discovery_data_t
{
    int          user_rank;
    int          hcl_rank;
    int          host_id;
    HCL_UniqueId hcl_uniqueId;
};

struct hccl_rank_discover_data_payload_t
{
    hccl_rank_discovery_data_t discovery_data;
};

struct hccl_bootstrap_general_payload_t
{
    int rank;
};

typedef enum hcclOp
{
    eHCCLReduce        = 0,
    eHCCLAllReduce     = 1,
    eHCCLReduceScatter = 2,
    eHCCLBroadcast     = 3,
    eHCCLAllGather     = 4,
    eHCCLAllToAll      = 5,
    eHCCLCollectiveMax = 5,    // last collective API
    eHCCLSend          = 6,
    eHCCLRecv          = 7,
    eHCCLOpMax         = 7,
} hcclOp;

/**
 * @brief collective call parameters
 * used as call signature in the collective log
 */
struct CollectiveParamsSignature
{
    size_t         count = 0;       // elements count
    hcclDataType_t datatype = hcclFloat32;    // data type
    hcclRedOp_t    reduceOp = hcclOpNone;    // reduce operation for reduction APIs
    int            peer = -1;        // peer rank when valid
    int            root = -1;        // root rank when valid

    bool operator==(const CollectiveParamsSignature &other) const
    {
        return (count    == other.count &&
                datatype == other.datatype &&
                reduceOp == other.reduceOp &&
                peer     == other.peer &&
                root     == other.root);
    }
};

/**
 * @brief send/recv pair calls signature
 * used as send/recv pair calls signature in collective log
 */
struct SendRecvSignature
{
    int            sender   = -1;           // sending rank
    int            receiver = -1;           // receiving rank
    size_t         count    = 0;            // elements count
    hcclDataType_t datatype = hcclFloat32;  // data type

    bool operator==(const SendRecvSignature& other) const
    {
        return (sender   == other.sender &&
                receiver == other.receiver &&
                count    == other.count &&
                datatype == other.datatype);
    }
};

/**
 * @brief collective log message data
 * sent from client ranks to coordinator
 */
struct CollectiveLogMessage
{
    int64_t timestamp;                  // system_clock time_point
    int     rank;                       // caller rank

    // operation parameters
    hcclOp                    op;       // API operation
    CollectiveParamsSignature params;   // call parameters

    bool bootstrapValidationError = false;

    CollectiveLogMessage(int _rank, hcclOp _op, CollectiveParamsSignature _params)
    : rank(_rank), op(_op), params(_params)
    {
    }
    CollectiveLogMessage(int _rank, bool _bootstrapValidationError)
    : rank(_rank), bootstrapValidationError(_bootstrapValidationError)
    {
    }
};

class hccl_communicator;

struct hcclInternalHandle
{
    std::atomic<bool> state {false};
    bool              result {true};

    bool waitForHandle(std::chrono::microseconds timeout = std::chrono::seconds(120))
    {
        auto expired = std::chrono::steady_clock::now() + timeout;
        while (true)
        {
            if (this->state)
            {
                return result;
            }
            else if (std::chrono::steady_clock::now() > expired)
            {
                LOG_HCL_ERR(HCL, "waitForHandle timeout.");
                return false;
            }

            std::this_thread::yield();
        }
    }

    void setHandleAsDone() { this->state = true; }
};

struct hcclOfiHandle
{
    ofi_req_t* req {nullptr};
    ofiComm_t* ofiComm {nullptr};
    void*      recvBuffer {nullptr};
    int        size {0};
};

struct hcclHandle
{
    hcclHandle() {};

    bool  isOfiReq {false};
    void* buffer {nullptr};

    union
    {
        hcclInternalHandle internalHandle {};
        hcclOfiHandle      ofi;
    };
};

struct hcclOpParams
{
    hcclOpParams(hcclOp             op,
                 const void*        sendbuff,
                 void*              recvbuff,
                 size_t             count,
                 hcclDataType_t     datatype,
                 hccl_communicator* comm,
                 synStreamHandle    stream_handle,
                 uint8_t            apiId,
                 hcclRedOp_t        reduceOp = hcclOpNone,
                 int                peer     = INT_MAX /* TODO - change to INVALID_HCCL_RANK */,
                 int                root     = INT_MAX)
    {
        this->m_op            = op;
        this->m_sendbuff      = sendbuff;
        this->m_recvbuff      = recvbuff;
        this->m_count         = count;
        this->m_datatype      = datatype;
        this->m_comm          = comm;
        this->m_stream_handle = stream_handle;
        this->m_apiId         = apiId;
        this->m_reduceOp      = reduceOp;
        this->m_peer          = peer;
        this->m_root          = root;
        this->m_handle        = std::make_shared<hcclHandle>();
    }

    hcclOp                      m_op;
    const void*                 m_sendbuff;
    void*                       m_recvbuff;
    size_t                      m_count;
    hcclDataType_t              m_datatype;
    hccl_communicator*          m_comm;
    synStreamHandle             m_stream_handle;
    uint8_t                     m_apiId;
    hcclRedOp_t                 m_reduceOp;
    int                         m_peer;
    int                         m_root;
    std::shared_ptr<hcclHandle> m_handle;
};
