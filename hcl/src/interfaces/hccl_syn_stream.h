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

#include <cstdint>              // for uint32_t
#include "hccl_types.h"         // for hcclResult_t
#include "synapse_api_types.h"  // for synStreamHandle

class synapse_stream_wrap
{
public:
    static synapse_stream_wrap* create(const synDeviceId deviceId, const uint32_t flags = 0);
    hcclResult_t                destroy();

    synStreamHandle getStreamHandle() const { return m_streamHandle; }

private:
    synapse_stream_wrap(synStreamHandle streamHandle) : m_streamHandle(streamHandle) {}
    synStreamHandle m_streamHandle;
};
