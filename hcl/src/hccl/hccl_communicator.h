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

#include <cstddef>                                // for size_t
#include <cstdint>                                // for uint64_t, uint8_t
#include <map>                                    // for map
#include <memory>                                 // for unique_ptr
#include <utility>                                // for move
#include <vector>                                 // for vector
#include "hccl_coordinator_client.h"              // for spHcclCoordinatorCl...
#include "hccl_types.h"                           // for hcclResult_t, hcclD...
#include "hcl_api_types.h"                        // for HCL_CollectiveOp
#include "interfaces/hcl_idevice.h"               // for IHclDevice
#include "ofi_communicator.h"                     // for host_communicator_h...
#include "interfaces/hcl_unique_sorted_vector.h"  // for UniqueSortedVector
#include "synapse_api_types.h"                    // for synStreamHandle
#include "synapse_common_types.h"                 // for synDataType, synDmaDir
#include "hcl_dynamic_communicator.h"

class ofi_component_t;

struct hcclHandle;
struct hcclOpParams;
struct internal_unique_id_t;

struct RankInfo;


class hccl_communicator
{
public:
    hccl_communicator(int rank, int comm_size);

    hcclResult_t initialize(const internal_unique_id_t* comm_unique_id);

    hcclResult_t sendCollectiveLogErr();

    bool destroy();

    void finalize();

    hcclResult_t comm_count(int* count);

    hcclResult_t get_async_error(hcclResult_t* asyncError);

    hcclResult_t syn_device(int* device);

    hcclResult_t comm_user_rank(int* rank);

    // * * * Collectives * * *

    hcclResult_t allreduce(const void*     sendbuff,
                           void*           recvbuff,
                           size_t          count,
                           hcclDataType_t  datatype,
                           hcclRedOp_t     reduceOp,
                           synStreamHandle stream_handle,
                           const uint32_t  flags,
                           uint8_t         apiId);

    hcclResult_t reduce(const void*     sendbuff,
                        void*           recvbuff,
                        size_t          count,
                        hcclDataType_t  datatype,
                        hcclRedOp_t     reduceOp,
                        int             root,
                        synStreamHandle stream_handle,
                        const uint32_t  flags,
                        uint8_t         apiId);

    hcclResult_t reduce_scatter(const void*     sendbuff,
                                void*           recvbuff,
                                size_t          recvcount,
                                hcclDataType_t  datatype,
                                hcclRedOp_t     reduceOp,
                                synStreamHandle stream_handle,
                                const uint32_t  flags,
                                uint8_t         apiId);

    hcclResult_t broadcast(const void*     sendbuff,
                           void*           recvbuff,
                           size_t          count,
                           hcclDataType_t  datatype,
                           int             root,
                           synStreamHandle stream_handle,
                           const uint32_t  flags,
                           uint8_t         apiId);

    hcclResult_t allgather(const void*     sendbuff,
                           void*           recvbuff,
                           size_t          sendcount,
                           hcclDataType_t  datatype,
                           synStreamHandle stream_handle,
                           const uint32_t  flags,
                           uint8_t         apiId);

    hcclResult_t alltoall(const void*     sendbuff,
                          void*           recvbuff,
                          size_t          count,
                          hcclDataType_t  datatype,
                          synStreamHandle stream_handle,
                          const uint32_t  flags,
                          uint8_t         apiId);

    // * * * Point-to-point

    hcclResult_t hccl_receive(void*           recvbuff,
                              size_t          count,
                              hcclDataType_t  datatype,
                              int             peer,
                              synStreamHandle stream_handle,
                              uint8_t         apiId);

    hcclResult_t hccl_send(const void*     sendbuff,
                           size_t          count,
                           hcclDataType_t  datatype,
                           int             peer,
                           synStreamHandle stream_handle,
                           uint8_t         apiId);

    size_t            getCommSize() const;
    const std::string getCommUniqueId();

    int user_rank() const;

    spHcclCoordinatorClient getCoordClient() { return m_coordClient; };

    const uint64_t getCollectiveCtr();
    void           incCollectiveCtr();

    const uint64_t getSendCtr(int peer);
    const uint64_t incSendCtr(int peer);
    const uint64_t getRecvCtr(int peer);
    const uint64_t incRecvCtr(int peer);

private:
    hcclResult_t openConnections(bool isLoopbackModeOrNullSubmission);

    hcclResult_t firstHandShakeAtInit(RankInfoHeader& header, std::vector<RankInfoHeader>& hcclRankInfoHeaders);

    hcclResult_t secondHandShakeAtInit(std::vector<RemoteDeviceConnectionInfo>& hcclRemoteDevices,
                                       bool                                     isLoopbackModeOrNullSubmission);

    void initializeRanks(std::vector<RankInfoHeader>& hcclRankInfoHeaders,
                         uint32_t                     commSize,
                         bool                         isLoopbackModeOrNullSubmission);

    hcclResult_t initializeConnections(bool isLoopbackModeOrNullSubmission);

    hcclResult_t finalizeInitialization(std::vector<RemoteDeviceConnectionInfo>& hcclRemoteDevices,
                                        bool                                     isLoopbackModeOrNullSubmission);

    void buildSecondHandShakeRemoteInfoBuffer(RankInfoBuffer& rankInfoBuffer);

    bool syncBetweenRanks();

    int m_rank;

    void updateRemoteDevices(std::vector<RankInfoHeader>& hcclRankInfo);
    void updateRemoteDevices(std::vector<RemoteDeviceConnectionInfo>& hcclRemoteDevices);

    spHcclCoordinatorClient m_coordClient;
    int                     m_boxSize;
    size_t                  m_commSize;
    bool                    m_scaleout_available;

    HclDynamicCommunicator* m_comm = nullptr;

    synEventHandle m_pdmaEventHandle;
};
