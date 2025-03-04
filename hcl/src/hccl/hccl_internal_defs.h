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
#include <atomic>           // for atomic
#include <bits/stdc++.h>    // for INT_MAX
#include "hccl_types.h"     // for hcclResult_t, hcclComm_t, hcc...
#include <hcl_api_types.h>  // for HCL_UniqueId
#include "hcl_utils.h"      // for LOG_HCL_

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

/**
 * @brief collective call parameters
 * used as call signature in the collective log
 */
struct CollectiveParamsSignature
{
    size_t         count    = 0;                 // elements count
    hcclDataType_t datatype = hcclFloat32;       // data type
    hcclRedOp_t    reduceOp = hcclOpNone;        // reduce operation for reduction APIs
    HCL_Rank       peer     = HCL_INVALID_RANK;  // peer rank when valid
    HCL_Rank       root     = HCL_INVALID_RANK;  // root rank when valid

    bool operator==(const CollectiveParamsSignature& other) const
    {
        return (count == other.count && datatype == other.datatype && reduceOp == other.reduceOp &&
                peer == other.peer && root == other.root);
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
        return (sender == other.sender && receiver == other.receiver && count == other.count &&
                datatype == other.datatype);
    }
};

/**
 * @brief collective log message data
 * sent from client ranks to coordinator
 */
struct CollectiveLogMessage
{
    int64_t  timestamp;  // system_clock time_point
    HCL_Rank rank;       // caller rank

    // operation parameters
    HCL_CollectiveOp          op;      // API operation
    CollectiveParamsSignature params;  // call parameters

    bool customError      = false;
    char errorString[256] = {};

    auto& operator=(const std::string& error)
    {
        customError = true;
        strncpy(errorString, error.c_str(), sizeof(errorString) - 1);
        return *this;
    }

    CollectiveLogMessage() = default;

    CollectiveLogMessage(HCL_Rank _rank, HCL_CollectiveOp _op, CollectiveParamsSignature _params)
    : rank(_rank), op(_op), params(_params)
    {
    }
};

class hccl_communicator;

struct hcclOfiHandle
{
    ofi_req_t* req {nullptr};
    void*      recvBuffer {nullptr};
    int        size {0};
};

struct hcclHandle
{
    hcclHandle() {};

    void*         buffer {nullptr};
    hcclOfiHandle ofi;
};

struct hcclOpParams
{
    hcclOpParams(HCL_CollectiveOp   op,
                 const void*        sendbuff,
                 void*              recvbuff,
                 size_t             count,
                 hcclDataType_t     datatype,
                 hccl_communicator* comm,
                 synStreamHandle    stream_handle,
                 uint8_t            apiId,
                 hcclRedOp_t        reduceOp = hcclOpNone,
                 int                peer     = INT_MAX,
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

    HCL_CollectiveOp            m_op;
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
