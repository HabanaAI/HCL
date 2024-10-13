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
#include <cstdint>                                  // for uint64_t
#include <memory>                                   // for allocator_traits<>:...
#include "hccl_communicator.h"                      // for hccl_communicator
#include "hccl_coordinator_client.h"                // for HcclCoordinatorClient
#include "hccl_helpers.h"                           // for hccl_data_type_elem...
#include "hccl_internal_defs.h"                     // for hcclHandle
#include "hccl_types.h"                             // for hcclSuccess, hcclRe...
#include "platform/gen2_arch_common/hccl_device.h"  // for HclApi
#include "hcl_api_types.h"                          // for HCL_Rank
#include "hcl_types.h"                              // for HclConfigType, LOOP...
#include "hcl_utils.h"                              // for LOG_HCL_ERR
#include "ofi_communicator.h"                       // for ofi_communicator
#include "libfabric/mr_mapping.h"                   // for MRMapping
#include "hcl_log_manager.h"                        // for LOG_ERR
#include "synapse_api_types.h"                      // for synStreamHandle
#include "hcl_dynamic_communicator.h"
#include "hcl_api_types.h"
#include "hcl_dynamic_communicator.h"
#include "hcl_api_types.h"

hcclResult_t hccl_communicator::hccl_receive(void*           recvbuff,
                                             size_t          count,
                                             hcclDataType_t  dataType,
                                             int             peer,
                                             synStreamHandle streamHandle,
                                             uint8_t         apiId)
{
    SendRecvApiEntry entry {ApiType::Recv,
                            apiId,
                            streamHandle,
                            reinterpret_cast<uint64_t>(recvbuff),
                            count,
                            dataType,
                            (HCL_Rank)peer,
                            *m_comm,
                            m_comm->m_remoteDevices[peer]->header.hwModuleID,
                            m_comm->isRankInsideScaleupGroup(peer)};

    return hccl_device().send_recv_call(m_comm->getMyRank(), entry);
}

hcclResult_t hccl_communicator::hccl_send(const void*     sendbuff,
                                          size_t          count,
                                          hcclDataType_t  dataType,
                                          int             peer,
                                          synStreamHandle streamHandle,
                                          uint8_t         apiId)
{
    SendRecvApiEntry entry {ApiType::Send,
                            apiId,
                            streamHandle,
                            reinterpret_cast<uint64_t>(sendbuff),
                            count,
                            dataType,
                            (HCL_Rank)peer,
                            *m_comm,
                            m_comm->m_remoteDevices[peer]->header.hwModuleID,
                            m_comm->isRankInsideScaleupGroup(peer)};

    return hccl_device().send_recv_call(m_comm->getMyRank(), entry);
}
