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

#include <cstddef>  // for size_t
#include <cstdint>  // for uint*
#include <vector>   // for vector
#include <mutex>    // for mutex, condition_variable

#include "hccl_types.h"                           // for hcclResult_t, hcclD...
#include "hcl_api_types.h"                        // for HCL_CollectiveOp
#include "interfaces/hcl_idevice.h"               // for IHclDevice
#include "ofi_communicator.h"                     // for host_communicator_h...
#include "interfaces/hcl_unique_sorted_vector.h"  // for UniqueSortedVector
#include "hcl_dynamic_communicator.h"             // for HclDynamicCommunicator
#include "coordinator/qp_migration.h"             // for IMigrationCallback
#include "coordinator_defs.h"

class hccl_communicator : public IMigrationCallback
{
public:
    hccl_communicator(int rank, int comm_size);
    virtual ~hccl_communicator() = default;

    hcclResult_t initialize(const internal_unique_id_t* comm_unique_id);

    hcclResult_t sendCollectiveLogErr();

    bool destroy();

    void finalize(bool lockStreams = true);

    hcclResult_t update_comm();

    hcclResult_t comm_count(int* count);

    hcclResult_t get_async_error(hcclResult_t* asyncError);

    hcclResult_t comm_user_rank(int* rank);

    // * * * Collectives * * *

    hcclResult_t allreduce(const void*    sendbuff,
                           void*          recvbuff,
                           size_t         count,
                           hcclDataType_t datatype,
                           hcclRedOp_t    reduceOp,
                           void*          stream_handle,
                           const uint32_t flags,
                           uint8_t        apiId);

    hcclResult_t reduce(const void*    sendbuff,
                        void*          recvbuff,
                        size_t         count,
                        hcclDataType_t datatype,
                        hcclRedOp_t    reduceOp,
                        int            root,
                        void*          stream_handle,
                        const uint32_t flags,
                        uint8_t        apiId);

    hcclResult_t reduce_scatter(const void*    sendbuff,
                                void*          recvbuff,
                                size_t         recvcount,
                                hcclDataType_t datatype,
                                hcclRedOp_t    reduceOp,
                                void*          stream_handle,
                                const uint32_t flags,
                                uint8_t        apiId);

    hcclResult_t broadcast(const void*    sendbuff,
                           void*          recvbuff,
                           size_t         count,
                           hcclDataType_t datatype,
                           int            root,
                           void*          stream_handle,
                           const uint32_t flags,
                           uint8_t        apiId);

    hcclResult_t allgather(const void*    sendbuff,
                           void*          recvbuff,
                           size_t         sendcount,
                           hcclDataType_t datatype,
                           void*          stream_handle,
                           const uint32_t flags,
                           uint8_t        apiId);

    hcclResult_t alltoall(const void*    sendbuff,
                          void*          recvbuff,
                          size_t         count,
                          hcclDataType_t datatype,
                          void*          streamHandle,
                          const uint32_t flags,
                          uint8_t        apiId);

    // * * * Point-to-point

    hcclResult_t
    hccl_receive(void* recvbuff, size_t count, hcclDataType_t datatype, int peer, void* stream_handle, uint8_t apiId);

    hcclResult_t hccl_send(const void*    sendbuff,
                           size_t         count,
                           hcclDataType_t datatype,
                           int            peer,
                           void*          stream_handle,
                           uint8_t        apiId);

    size_t            getCommSize() const;
    const std::string getCommUniqueId();

    int user_rank() const;

    spHcclCoordinatorClient getCoordClient() { return m_coordClient; };

    const uint64_t getCollectiveCtr() const;
    void           incCollectiveCtr();

    const uint64_t getSendCtr(const HCL_Rank peer) const;
    const uint64_t incSendCtr(const HCL_Rank peer);
    const uint64_t getRecvCtr(const HCL_Rank peer) const;
    const uint64_t incRecvCtr(const HCL_Rank peer);

    HclDynamicCommunicator*       getDynamicComm();
    const HclDynamicCommunicator& getDynamicCommConst() const;

    virtual void mcNicStateChange(const NicState& nicState) override;

    void checkFaultToleranceStopCommCollApiUntil();  // called by HCL API functions to check if to stop comm specific
                                                     // collectives API's

    void checkFaultToleranceStopCommSendRecvApiUntil();  // called by HCL API group end to check if to stop comm
                                                         // specific S/R API's

private:
    hcclResult_t openConnections(bool isLoopbackModeOrNullSubmission);

    hcclResult_t exchangeRankData(RankInfoHeader& header, std::vector<RankInfoHeader>& hcclRankInfoHeaders);

    hcclResult_t exchangeQpsData(bool isLoopbackModeOrNullSubmission);

    void initializeRanks(std::vector<RankInfoHeader>& hcclRankInfoHeaders,
                         uint32_t                     commSize,
                         bool                         isLoopbackModeOrNullSubmission);

    hcclResult_t initializeConnections(bool isLoopbackModeOrNullSubmission);

    hcclResult_t finalizeInitialization(bool isLoopbackModeOrNullSubmission);

    void prepareQPsInfo(RankInfoBuffer& rankInfoBuffer) const;
    void buildMigrationAndCountersDataExchangeBuffer(remote_devices_t& remoteDevicesData, const bool failover);

    bool rendezvous();

    HCL_Rank m_rank;

    void     updateRemoteDevices(std::vector<RankInfoHeader>& hcclRankInfo);
    void     updateRemoteDevices(std::vector<RemoteDeviceConnectionInfo>& hcclRemoteDevices);
    uint64_t getAccumulatedMask(const std::vector<RankInfoHeader>& RankInfoHeaders) const;

    hcclResult_t updateScaleoutPortMask(const std::vector<RankInfoHeader>& RankInfoHeaders);

    void faultToleranceStopAllApis() const;  // Stop all API's globally (done once for all comms)
    void faultToleranceStopCommApisUntil(
        const RankApiCounters& stopUntil);     // Stop all collectives and S/R API's until specific counters set
    void faultToleranceStopCommAllApis();      // Stop all collective and S/R API calls
    void faultToleranceResumeAllApis() const;  // Resumes all API's globally (done once for all comms)
    void faultToleranceResumeCommApis();       // Resume all collective and S/R API calls
    void faultTolerancePrepareMySendRecvCounters(RankInfoBuffer& rankInfoBuffer) const;
    void faultTolerancePrepareMyCollectivesApiCounter(RankInfoBuffer& rankInfoBuffer) const;
    void faultToleranceCalcAllRanksMaxCounters(RankApiCounters&        maxRankApiCountersData,
                                               const remote_devices_t& hcclRemoteDevices);
    void faultTolerancePollUntilApiReach(const RankApiCounters& maxRankApiCountersData);
    void mcNicStateShutdown(const uint32_t logicalPort);
    void mcNicStateUp(const uint32_t logicalPort);
    void stopApis();
    void resumeUntil(const RankApiCounters& maxRankApiCountersData);
    void resumeApis();

    spHcclCoordinatorClient m_coordClient;
    int                     m_boxSize;
    size_t                  m_commSize;
    bool                    m_scaleout_available;

    HclDynamicCommunicator* m_comm = nullptr;

    std::condition_variable m_faultsStopCommApiCv;        // CV to block user API threads on specific comm
    std::mutex              m_faultsStopCommApiMutex;     // Mutex for above CV
    RankApiCounters         m_faultStopUntilApiCounters;  // To stop collective and S/R API calls until this limit.
                                                          // When ULLONG_MAX its unblocked.
                                                          // when 0 its blocked unconditionally

    nics_mask_t m_lkdBadMask = 0;
};
