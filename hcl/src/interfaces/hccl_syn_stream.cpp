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

#include "hccl_syn_stream.h"

#include "hccl_helpers.h"          // for to_hccl_result
#include "hcl_log_manager.h"       // for LOG_ERR
#include "synapse_api.h"           // for synStreamCreate, synStreamDestroy
#include "synapse_common_types.h"  // for synStatus, synSuccess

synapse_stream_wrap* synapse_stream_wrap::create(const synDeviceId deviceId, const uint32_t flags)
{
    synStreamHandle streamHandle;
    synStatus       status {synStreamCreateGeneric(&streamHandle, deviceId, flags)};
    if (status != synSuccess)
    {
        LOG_ERR(HCL, "synStreamCreateGeneric failed with synStatus={}.", status);
        return nullptr;
    }

    return new synapse_stream_wrap(streamHandle);
}

hcclResult_t synapse_stream_wrap::destroy()
{
    synStatus status = synStreamDestroy(m_streamHandle);
    if (status != synSuccess)
    {
        LOG_ERR(HCL, "synStreamDestroy failed with status={}.", status);
    }

    return to_hccl_result(status);
}