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

#include <cstddef>               // for size_t
#include <cstdint>               // for uint32_t
#include <memory>                // for shared_ptr
#include <vector>                // for vector
#include "hccl_internal_defs.h"  // for hccl_rank_discovery_data_t (ptr only)
#include "hccl_types.h"          // for hcclResult_t
#include "socket_thread.h"       // for SocketThreadsManager

#include "../coordinator/coordinator_defs.h"

class HcclCoordinatorClient : public IHcclCoordinatorClient
{
public:
    HcclCoordinatorClient(int nranks, HCL_Rank rank, const internal_unique_id_t* internalUniqueId);
    ~HcclCoordinatorClient()                                   = default;
    HcclCoordinatorClient(HcclCoordinatorClient&)              = delete;
    HcclCoordinatorClient(HcclCoordinatorClient&&)             = delete;
    HcclCoordinatorClient&  operator=(HcclCoordinatorClient&)  = delete;
    HcclCoordinatorClient&& operator=(HcclCoordinatorClient&&) = delete;

    virtual bool destroy() override;
    virtual bool
    commInitHandshake1(int nranks, RankInfoHeader& myRankInfo, std::vector<RankInfoHeader>& ranksInfo) override;
    virtual bool commInitHandshake2(int                                      nranks,
                                    void*                                    rankInfoBuffer,
                                    uint32_t                                 rankInfoBufferSize,
                                    std::vector<RemoteDeviceConnectionInfo>& remoteDevicesInfo) override;
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

private:
    bool         closeBootstrapNetwork();
    hcclResult_t sendCollectiveLogMsg(CollectiveLogMessage& msg);

    hcclResult_t sendToRank(HCL_Rank peer, void* data, uint32_t size);
    hcclResult_t recvFromRankAsync(void* data, int size, HCL_Rank peer, hcclHandle* handle);

    hcclResult_t recvFromCoordinator(int socket, void* data, uint64_t size);
    hcclResult_t sendToCoordinator(int socket, void* data, uint64_t size);
    bool         bootstrapMsgExchange(int           coordinatorSocket,
                                      msg_header_t& hdr,
                                      const void*   msg,
                                      size_t        msgSize,
                                      void*         recvBuffer,
                                      size_t        recvSize);
    bool         sendGeneralMsg(int coordinatorSocket, bootstrap_hdr_id_t headerId);
    void
    openSocketWithCoordinator(int& newSocket, const internal_unique_id_t* internalUniqueId, bootstrapSocketType type);

    HCL_Rank             m_rank;
    int                  m_nranks;
    int                  m_mainSocket      = -1;
    int                  m_asyncRecvSocket = -1;
    int                  m_logSocket       = -1;  // collective logs non-blocking socket
    SocketThreadsManager m_threadManager;

    std::vector<uint32_t> m_sendSequence;
    std::vector<uint32_t> m_recvSequence;
};
