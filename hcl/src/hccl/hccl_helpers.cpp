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

#include <ostream>  // for operator<<, basic_ostream
#include "hccl_helpers.h"
#include "hcl_utils.h"
#include "hccl_types.h"            // for hcclResult_t, hcclBusy, hcclInternalError, hccl...
#include "synapse_common_types.h"  // for synAllResourcesTaken, synBusy, syn...

hcclResult_t to_hccl_result(const hcclResult_t status)
{
    return status;
}

std::string to_string(const hcclResult_t status)
{
    switch (status)
    {
        case hcclSuccess:
            return "hcclSuccess";
        case hcclInternalError:
            return "hcclInternalError";
        case hcclInvalidArgument:
            return "hcclInvalidArgument";
        case hcclBusy:
            return "hcclBusy";
        case hcclUnsupported:
            return "hcclUnsupported";
        default:
            return "<invalid-hcl-status:" + std::to_string(static_cast<int>(status)) + ">";
    }
}

std::string to_string(const synStatus status)
{
    switch (status)
    {
        case synSuccess:
            return "synSuccess";
        case synInvalidArgument:
            return "synInvalidArgument";
        case synCbFull:
            return "synCbFull";
        case synOutOfHostMemory:
            return "synOutOfHostMemory";
        case synOutOfDeviceMemory:
            return "synOutOfDeviceMemory";
        case synObjectAlreadyInitialized:
            return "synObjectAlreadyInitialized";
        case synObjectNotInitialized:
            return "hcclInvalidUsage";
        case synCommandSubmissionFailure:
            return "synCommandSubmissionFailure";
        case synNoDeviceFound:
            return "synNoDeviceFound";
        case synDeviceTypeMismatch:
            return "synDeviceTypeMismatch";
        case synFailedToInitializeCb:
            return "synFailedToInitializeCb";
        case synFailedToFreeCb:
            return "synFailedToFreeCb";
        case synFailedToMapCb:
            return "synFailedToMapCb";
        case synFailedToUnmapCb:
            return "synFailedToUnmapCb";
        case synFailedToAllocateDeviceMemory:
            return "synFailedToAllocateDeviceMemory";
        case synFailedToFreeDeviceMemory:
            return "synFailedToFreeDeviceMemory";
        case synFailedNotEnoughDevicesFound:
            return "synFailedNotEnoughDevicesFound";
        case synDeviceReset:
            return "synDeviceReset";
        case synUnsupported:
            return "synUnsupported";
        case synWrongParamsFile:
            return "synWrongParamsFile";
        case synDeviceAlreadyAcquired:
            return "synDeviceAlreadyAcquired";
        case synNameIsAlreadyUsed:
            return "synNameIsAlreadyUsed";
        case synBusy:
            return "synBusy";
        case synAllResourcesTaken:
            return "hcclSystemError";
        case synUnavailable:
            return "synUnavailable";
        case synFail:
            return "synFail";
        default:
            return "<invalid-syn-status:" + std::to_string(static_cast<int>(status)) + ">";
    }
}

// TODO (No JIRA Ticker): Improve.
hcclResult_t to_hccl_result(const synStatus status)
{
    switch (status)
    {
        case synSuccess:
            return hcclSuccess;
        case synInvalidArgument:
            return hcclInvalidArgument;
        case synCbFull:
            return hcclInternalError;
        case synOutOfHostMemory:
        case synOutOfDeviceMemory:
            return hcclOutOfMemory;
        case synObjectAlreadyInitialized:
        case synObjectNotInitialized:
            return hcclInvalidUsage;
        case synCommandSubmissionFailure:
            return hcclInternalError;
        case synNoDeviceFound:
        case synDeviceTypeMismatch:
            return hcclInvalidUsage;
        case synFailedToInitializeCb:
        case synFailedToFreeCb:
        case synFailedToMapCb:
        case synFailedToUnmapCb:
        case synFailedToAllocateDeviceMemory:
        case synFailedToFreeDeviceMemory:
            return hcclInternalError;
        case synFailedNotEnoughDevicesFound:
            return hcclInvalidUsage;
        case synDeviceReset:
            return hcclUnhandledSynapseError;
        case synUnsupported:
            return hcclInternalError;
        case synWrongParamsFile:
        case synDeviceAlreadyAcquired:
            return hcclInvalidUsage;
        case synNameIsAlreadyUsed:
        case synBusy:
        case synAllResourcesTaken:
            return hcclSystemError;
        case synUnavailable:
            return hcclUnhandledSynapseError;
        case synInvalidTensorDimensions:
            return hcclInvalidArgument;
        case synFail:
            return hcclInternalError;
        default:
            return hcclInternalError;
    }
    VERIFY(false, "Cannot convert '{}' to hcclResult_t.", to_string(status));
    return hcclInternalError;
}

std::string to_string(hcclRedOp_t reduction_op)
{
    switch (reduction_op)
    {
        case hcclSum:
            return "sum";
        case hcclProd:
            return "prod";
        case hcclMin:
            return "min";
        case hcclMax:
            return "max";
        case hcclAvg:
            return "avg";
        default:
            return "<invalid-reduction-op:" + std::to_string(static_cast<int>(reduction_op)) + ">";
    }
}

std::ostream& operator<<(std::ostream& os, const hcclRedOp_t& reduceOp)
{
    return os << to_string(reduceOp);
}

std::string to_string(hcclDataType_t data_type)
{
    switch (data_type)
    {
        case hcclInt8:
            return "int8";
        case hcclUint8:
            return "uint8";
        case hcclInt32:
            return "int32";
        case hcclUint32:
            return "uint32";
        case hcclInt64:
            return "int64";
        case hcclUint64:
            return "uint64";
        case hcclFloat16:
            return "fp16";
        case hcclFloat32:
            return "fp32";
        case hcclFloat64:
            return "fp64";
        case hcclBfloat16:
            return "bf16";
        default:
            return "<invalid-data-type:" + std::to_string(static_cast<int>(data_type)) + ">";
    }
}

size_t hccl_data_type_elem_size(hcclDataType_t data_type)
{
    switch (data_type)
    {
        case hcclInt8:
        case hcclUint8:
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
            return 1;
        case hcclInt32:
        case hcclUint32:
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
            return 4;
        case hcclInt64:
        case hcclUint64:
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
            return 8;
        case hcclFloat16:
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
            return 2;
        case hcclFloat32:
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
            return 4;
        case hcclFloat64:
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
            return 8;
        case hcclBfloat16:
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
            return 2;
        case hcclNumTypes:
            break;
    }
    VERIFY(false, "Cannot convert '{}' to synDataType.", to_string(data_type));
    return 0;
}

hcclDataType_t from_hccl_data_type_for_emulation(hcclDataType_t data_type)
{
    switch (data_type)
    {
        case hcclInt8:
        case hcclUint8:
            return hcclBfloat16;
        case hcclInt32:
        case hcclUint32:
        case hcclInt64:
        case hcclUint64:
            return hcclInt32;
        case hcclFloat16:
            return hcclBfloat16;
        case hcclFloat32:
            return hcclFloat32;
        case hcclFloat64:
            return hcclInt32;
        case hcclBfloat16:
            return hcclBfloat16;
        case hcclNumTypes:
            break;
    }
    VERIFY(false, "Encountered an invalid hcclDataType_t enum value of: {}", static_cast<int>(data_type));
    return hcclInt8;
}

const char* get_error_string(hcclResult_t result)
{
    switch (result)
    {
        case hcclSuccess:
            return "Function succeeded";
        case hcclNoDeviceFound:
            return "No device found or no device set";
        case hcclUnsupported:
            return "Unsupported feature or usage model";
        case hcclUnhandledSynapseError:
            return "A call to Synapse failed";
        case hcclSystemError:
            return "A call to the system failed";
        case hcclInternalError:
            return "A device is busy or an internal check failed";
        case hcclInvalidArgument:
            return "One argument has an invalid value";
        case hcclInvalidUsage:
            return "The call to HCCL is incorrect";
        case hcclSocketError:
            return "Problem with HCCL initialization occurred.";
        default:
            return "An unknown or an invalid error";
    }
}

synDataType to_synapse_data_type(hcclDataType_t data_type)
{
    switch (data_type)
    {
        case hcclInt8:
            // case hcclChar:
            return syn_type_int8;
        case hcclUint8:
            return syn_type_uint8;
        case hcclInt32:
            // case hcclInt:
            return syn_type_int32;
        case hcclUint32:
            return syn_type_uint32;
        case hcclFloat16:
            // case hcclHalf:
            return syn_type_fp16;
        case hcclFloat32:
            // case hcclFloat:
            return syn_type_float;
        case hcclBfloat16:
            return syn_type_bf16;

        case hcclInt64:
        case hcclUint64:
        case hcclFloat64:
        // case hcclDouble:
        case hcclNumTypes:
        default:
            VERIFY(false, "Cannot convert '{}' to synDataType.", to_string(data_type));
            return {};
    }
}
