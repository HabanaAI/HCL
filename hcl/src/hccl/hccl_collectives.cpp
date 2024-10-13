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

#include <cstddef>                                  // for size_t
#include <cstdint>                                  // for uint64_t, int64_t
#include <vector>                                   // for vector
#include "hccl_communicator.h"                      // for hccl_communicator
#include "hccl_internal_defs.h"                     // for hcclOpParams, eHCCL...
#include "hccl_types.h"                             // for hcclResult_t, hcclS...
#include "platform/gen2_arch_common/hccl_device.h"  // for HclApi
#include "hcl_api_types.h"                          // for eHCLNoFlag, HCL_Rank
#include "hcl_global_conf.h"                        // for GCFG_BOX_TYPE_ID
#include "hcl_types.h"                              // for HclConfigType, LOOP...
#include "hcl_utils.h"                              // for LOG_HCL_TRACE
#include "hcl_log_manager.h"                        // for LOG_TRACE
#include "synapse_api_types.h"                      // for synStreamHandle
#include "hcl_dynamic_communicator.h"

hcclResult_t hccl_communicator::allreduce(const void*     sendbuff,
                                          void*           recvbuff,
                                          size_t          count,
                                          hcclDataType_t  dataType,
                                          hcclRedOp_t     reduceOp,
                                          synStreamHandle stream_handle,
                                          const uint32_t  flags,
                                          uint8_t         apiId)
{
    HclCollectiveParams params(eHCLAllReduce,
                               stream_handle,
                               reinterpret_cast<uint64_t>(sendbuff),
                               reinterpret_cast<uint64_t>(recvbuff),
                               count,
                               dataType,
                               *m_comm,
                               apiId,
                               flags,
                               reduceOp);

    return hccl_device().collective_call(params);
}

hcclResult_t hccl_communicator::reduce(const void*     sendbuff,
                                       void*           recvbuff,
                                       size_t          count,
                                       hcclDataType_t  dataType,
                                       hcclRedOp_t     reduceOp,
                                       int             root,
                                       synStreamHandle stream_handle,
                                       const uint32_t  flags,
                                       uint8_t         apiId)
{
    HclCollectiveParams params(eHCLReduce,
                               stream_handle,
                               reinterpret_cast<uint64_t>(sendbuff),
                               reinterpret_cast<uint64_t>(recvbuff),
                               count,
                               dataType,
                               *m_comm,
                               apiId,
                               flags,
                               reduceOp,
                               root);

    return hccl_device().collective_call(params);
}

hcclResult_t hccl_communicator::reduce_scatter(const void*     sendBuff,
                                               void*           recvBuff,
                                               size_t          recvCount,
                                               hcclDataType_t  dataType,
                                               hcclRedOp_t     reduceOp,
                                               synStreamHandle streamHandle,
                                               const uint32_t  flags,
                                               uint8_t         apiId)
{
    size_t communicatorSize = m_commSize;

    // HCCL receives `recvCount`, which is the number of elements produced in the output buffer - just like NCCL does.
    // HCL operates on `sendCount`, which is the number of elements of the input buffer,
    // which is greater than `recvCount` times the number of HCL workers.
    const size_t sendCount = recvCount * communicatorSize;

    HclCollectiveParams params(eHCLReduceScatter,
                               streamHandle,
                               reinterpret_cast<uint64_t>(sendBuff),
                               reinterpret_cast<uint64_t>(recvBuff),
                               sendCount,
                               dataType,
                               *m_comm,
                               apiId,
                               flags,
                               reduceOp);

    return hccl_device().collective_call(params);
}

hcclResult_t hccl_communicator::alltoall(const void*     sendbuff,
                                         void*           recvbuff,
                                         size_t          count,
                                         hcclDataType_t  dataType,
                                         synStreamHandle stream_handle,
                                         const uint32_t  flags,
                                         uint8_t         apiId)
{
    HclCollectiveParams params(eHCLAll2All,
                               stream_handle,
                               reinterpret_cast<uint64_t>(sendbuff),
                               reinterpret_cast<uint64_t>(recvbuff),
                               count,
                               dataType,
                               *m_comm,
                               apiId,
                               flags);

    hccl_device().collective_call(params);

    return hcclSuccess;
}

hcclResult_t hccl_communicator::broadcast(const void*     sendbuff,
                                          void*           recvbuff,
                                          size_t          count,
                                          hcclDataType_t  dataType,
                                          int             root,
                                          synStreamHandle stream_handle,
                                          const uint32_t  flags,
                                          uint8_t         apiId)
{
    HclCollectiveParams params(eHCLBroadcast,
                               stream_handle,
                               (uint64_t)sendbuff,
                               (uint64_t)recvbuff,
                               count,
                               dataType,
                               *m_comm,
                               apiId,
                               flags,
                               hcclOpNone,
                               root);

    return hccl_device().collective_call(params);
}

hcclResult_t hccl_communicator::allgather(const void*     sendBuff,
                                          void*           recvBuff,
                                          size_t          sendCount,
                                          hcclDataType_t  dataType,
                                          synStreamHandle streamHandle,
                                          const uint32_t  flags,
                                          uint8_t         apiId)
{
    HclCollectiveParams params(eHCLAllGather,
                               streamHandle,
                               reinterpret_cast<uint64_t>(sendBuff),
                               reinterpret_cast<uint64_t>(recvBuff),
                               sendCount,
                               dataType,
                               *m_comm,
                               apiId,
                               flags);

    return hccl_device().collective_call(params);
}
