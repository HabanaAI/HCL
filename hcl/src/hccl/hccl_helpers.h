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

#include <cstddef>                      // for size_t
#include <string>                       // for string
#include "synapse_common_types.h"       // for synDataType, synStatus
#include "hccl_types.h"                 // for hcclDataType_t, hcclResult_t,...
#include "hcl_log_manager.h"            // for LOG_INFO, LOG_TRACE
#include "hcl_utils.h"                  // for checkReductionOp

#define RETURN_ON_COND(_condition_for_error, _result, _message)                                                        \
    {                                                                                                                  \
        if (_condition_for_error)                                                                                      \
        {                                                                                                              \
            LOG_ERR(HCL, "{}", _message);                                                                              \
            return to_hccl_result(_result);                                                                            \
        }                                                                                                              \
    }

#define RETURN_ON_ERROR(_result, _message) RETURN_ON_COND(_result != hcclSuccess, _result, _message)

#define RETURN_ON_SYNAPSE_ERROR(_result, _message) RETURN_ON_COND(_result != synSuccess, _result, _message)

#define RETURN_ON_HCL_ERROR(_result, _message) RETURN_ON_COND(_result != hcclSuccess, _result, _message)

#define RETURN_ON_INVALID_ARG(_condition_for_error, _arg, _message)                                                    \
    RETURN_ON_COND(_condition_for_error, hcclInvalidArgument, "Invalid argument '" #_arg "': " #_message)

#define RETURN_ON_NULL_ARG(_arg) RETURN_ON_INVALID_ARG(_arg == nullptr, _arg, "Cannot be null.")

// Support for fp16 not enabled
#define RETURN_ON_INVALID_DATA_TYPE(_arg)                                                                              \
    {                                                                                                                  \
        if (_arg != hcclFloat32 && _arg != hcclBfloat16 && _arg != hcclFloat16)                                                               \
        {                                                                                                              \
            LOG_ERR(HCL, "Invalid or unsupported data type {}.", to_string(_arg));                                     \
            return to_hccl_result(hcclInvalidArgument);                                                                \
        }                                                                                                              \
    }

#define RETURN_ON_INVALID_ADDR(addr)                                                                                   \
    {                                                                                                                  \
        bool valid = hccl_device()->isDramAddressValid((uint64_t)addr);                                        \
        RETURN_ON_INVALID_ARG(!valid, addr, "Invalid address");                                                        \
    }

#define RETURN_ON_INVALID_STREAM(_arg) RETURN_ON_INVALID_ARG(_arg == nullptr, _arg, "Invalid stream.")

#define RETURN_ON_INVALID_RANK(rank, commSize)                                                                         \
    RETURN_ON_INVALID_ARG(rank >= (int)commSize || rank < 0, rank, "Invalid rank")

#define RETURN_ON_INVALID_HCCL_COMM(_arg)                                                                              \
    RETURN_ON_INVALID_ARG(_arg == nullptr, _arg, "Invalid HCCL communicator handle.")

#define RETURN_ON_INVALID_REDUCTION_OP(_reduction_op)                                                                  \
    {                                                                                                                  \
        if (checkReductionOp(_reduction_op) == false)                                                                  \
        {                                                                                                              \
            LOG_ERR(HCL, "Invalid reduction op: {}", _reduction_op);                                                   \
            return hcclInvalidArgument;                                                                                \
        }                                                                                                              \
    }

#define RETURN_ON_INVALID_FD(_arg)                                                                                     \
    {                                                                                                                  \
        if (_arg == nullptr)                                                                                           \
        {                                                                                                              \
            LOG_ERR(HCL, "Invalid FD was provided.");                                                                  \
            return hcclInvalidArgument;                                                                                \
        }                                                                                                              \
    }

#define RETURN_ON_RANK_CHECK(rank, comm)                                                                               \
    if ((HclConfigType)GCFG_BOX_TYPE_ID.value() != HclConfigType::LOOPBACK)                                            \
    {                                                                                                                  \
        RETURN_ON_INVALID_RANK(rank, comm->getCommSize());                                                             \
    }



const char*  get_error_string(hcclResult_t result);
hcclResult_t to_hccl_result(const synStatus status);
hcclResult_t to_hccl_result(const hcclResult_t status);
hcclResult_t to_hccl_result(const hcclResult_t status);
std::string  to_string(hcclRedOp_t reduction_op);
std::string  to_string(hcclDataType_t data_type);
std::string  to_string(const synStatus status);

size_t         hccl_data_type_elem_size(hcclDataType_t data_type);
hcclDataType_t from_hccl_data_type_for_emulation(hcclDataType_t data_type);
synDataType to_synapse_data_type(hcclDataType_t data_type);


std::ostream& operator<<(std::ostream& os, const hcclRedOp_t& reduceOp);
