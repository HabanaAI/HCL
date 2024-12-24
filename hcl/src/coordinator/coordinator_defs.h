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
#include "collective_logger.h"
#include "interfaces/hcl_unique_sorted_vector.h"

class IHcclCoordinatorClient
{
public:
    virtual ~IHcclCoordinatorClient() = default;
    virtual bool
    exchangeRankInfo(int nranks, const RankInfoHeader& myRankInfo, std::vector<RankInfoHeader>& ranksInfo) = 0;

    virtual bool exchangeQpsInfo(int                                      nranks,
                                 const RankInfoBuffer&                    rankInfoBuffer,
                                 uint32_t                                 rankInfoBufferSize,
                                 std::vector<RemoteDeviceConnectionInfo>& remoteDevicesInfo) = 0;

    virtual bool rendezvous() = 0;

    virtual hcclResult_t sendCollectiveLog(const HCL_CollectiveOp op,
                                           const size_t           count,
                                           const hcclDataType_t   datatype,
                                           const hcclRedOp_t      reduceOp,
                                           const HCL_Rank         peer,
                                           const HCL_Rank         root) = 0;

    virtual hcclResult_t sendCollectiveLogErr() = 0;

    virtual hcclResult_t sendRecvFromRanks(UniqueSortedVector& nonPeerRemoteRanks,
                                           std::vector<void*>& recvBuffers,
                                           std::vector<void*>& sendBuffers,
                                           size_t              sendRecvBufSize) = 0;

    virtual bool rendezvous(const UniqueSortedVector& remoteRanks) = 0;

    virtual bool sendNicStateChange(const class NicState& nicState)         = 0;
    virtual bool exchangeMigrationData(int                   nranks,
                                       const RankInfoBuffer& myInfo,
                                       uint32_t              rankInfoBufferSize,
                                       remote_devices_t&     remoteDevicesInfo) = 0;

    class IMigrationCallback* migration_cb_ = nullptr;
};

using spHcclCoordinatorClient = std::shared_ptr<IHcclCoordinatorClient>;

using HcclCoordinatorUPtr = std::unique_ptr<class IHcclCoordinator>;

class IHcclCoordinator
{
public:
    static HcclCoordinatorUPtr create(bool use_global_comm_ip = false);
    virtual ~IHcclCoordinator() = default;

    size_t next_id()
    {
        static size_t id = CORD_ID_GLOBAL_COMM;
        return ++id;  // Start with 2, to distinguish from 0 (invalid) and 1 (global comm).
    }

    void get_unique_id(hcclUniqueId& unique_id)
    {
        VERIFY(sizeof(unique_id) == unique_id_buff_.size(), "Unexpected unique_id size={}", unique_id_buff_.size());
        std::memcpy(reinterpret_cast<void*>(&unique_id), unique_id_buff_.data(), unique_id_buff_.size());
    }

    int internal_id() { return internal_id_; }

protected:
    std::vector<uint8_t> unique_id_buff_;
    int                  internal_id_;
};
