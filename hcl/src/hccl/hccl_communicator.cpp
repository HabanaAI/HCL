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

#include "hccl_communicator.h"

#include <cstddef>        // for size_t
#include <cstdint>        // for uint*
#include <sstream>        // for basic_ostream::operator<<
#include <unordered_map>  // for unordered_map, unorder...
#include <mutex>          // for mutex, unique_lock
#include <memory>         // for unique_ptr
#include <vector>         // for vector

#include "hccl_helpers.h"        // for RETURN_ON_SYNAPSE_ERROR
#include "hccl_internal_defs.h"  // for hcclHandle, HOST_BUFF_INC
#include "hccl_types.h"          // for hcclSuccess, hcclResult_t
#include "platform/gen2_arch_common/hccl_device.h"
#include "hcl_api_types.h"               // for HCL_Comm, eHCLReduceSc...
#include "hcl_config.h"                  // for HclConfig
#include "hcl_dynamic_communicator.h"    // for HclDynamicCommunicator
#include "hcl_global_conf.h"             // for GlobalConfBool, GCFG_H...
#include "hcl_types.h"                   // for RankInfo, HclConfigType
#include "hcl_utils.h"                   // for LOG_HCL_ERR, VERIFY
#include "interfaces/hcl_idevice.h"      // for IHclDevice
#include "libfabric/hl_ofi.h"            // for ofi_t
#include "libfabric/hl_ofi_component.h"  // for ofi_component_t
#include "hcl_log_manager.h"             // for LOG_ERR, LOG_DEBUG
#include "ofi_communicator.h"            // for ofi_communicator
#include "synapse_common_types.h"        // for synStatus
#include "hcl_math_utils.h"
#include "platform/gen2_arch_common/server_def.h"  // for Gen2ArchServerDef
#include "hccl_api_inc.h"                          // for g_faultsCheckStopApi, g_faultsStopAllApi
#include "coordinator/hlcp_client.h"
#include "hccl_context.h"
#include "fault_tolerance_inc.h"  // for HLFT.* macros

std::unordered_map<HCL_Comm, spHcclCoordinatorClient> g_hcclCordClient;

hcclResult_t hccl_communicator::exchangeRankData(RankInfoHeader&              header,
                                                 std::vector<RankInfoHeader>& hcclRankInfoHeaders)
{
    hcclResult_t rc = hcclSuccess;
    if (!GCFG_HCL_NULL_SUBMIT.value())
    {
        if (!m_coordClient->exchangeRankInfo(m_commSize, header, hcclRankInfoHeaders))
        {
            LOG_HCL_ERR(HCL, "Handshake1 with remote ranks failed");
            rc = hcclInternalError;
        }
    }
    else
    {
        // filling all ranks with my info
        for (unsigned i = 0; i < hcclRankInfoHeaders.size(); i++)
        {
            hcclRankInfoHeaders[i]         = header;  // fill the remote headers with some info
            hcclRankInfoHeaders[i].boxSize = hccl_device()->getServerDef().getDefaultBoxSize();
        }
    }
    return rc;
}

void hccl_communicator::prepareQPsInfo(RankInfoBuffer& rankInfoBuffer) const
{
    rankInfoBuffer.localInfo.header = m_comm->m_rankInfo.header;
    rankInfoBuffer.localInfo.device = m_comm->m_rankInfo.device;

    memcpy(rankInfoBuffer.remoteInfo, m_comm->m_rankInfo.remoteInfo.data(), sizeof(RemoteInfo) * m_commSize);
}

hcclResult_t hccl_communicator::updateScaleoutPortMask(const std::vector<RankInfoHeader>& RankInfoHeaders)
{
    const uint64_t accumulatedFailedMask = getAccumulatedMask(RankInfoHeaders);

    if (hccl_device()->isScaleOutAvailable() &&
        (accumulatedFailedMask ==
         hccl_device()->getServerConnectivity().getExternalPortsMaskGlbl()) &&  // the failed mask is shared with hnic.
        (accumulatedFailedMask != 0))  // for hnic, the failed mask is always 0, so don't log an error for that case
    {
        LOG_HCL_ERR(HCL,
                    "No common scaleout ports between ranks. FailedMask {:024b} External mask {:024b}",
                    (uint64_t)accumulatedFailedMask,
                    (uint64_t)hccl_device()->getServerConnectivity().getExternalPortsMaskGlbl());
        return hcclInternalError;
    }

    const uint64_t accumulatedGoodMask = ~accumulatedFailedMask;
    m_comm->getCommConnectivity().updateScaleOutPortsMask(hccl_device()->getServerConnectivity(),
                                                          nics_mask_t(accumulatedGoodMask));

    return hcclSuccess;
}

hcclResult_t hccl_communicator::exchangeQpsData(bool isLoopbackModeOrNullSubmission)
{
    LOG_HCL_TRACE(HCL, "");
    hcclResult_t rc = hcclSuccess;

    if (isLoopbackModeOrNullSubmission)
    {
        return rc;
    }

    std::vector<RemoteDeviceConnectionInfo> hcclRemoteDevices(m_commSize);

    // RemoteInfo data size
    const uint32_t remoteSize = sizeof(RemoteInfo) * m_commSize;
    // total size to send
    const uint32_t rankInfoBufferSize = sizeof(LocalRankInfo) + remoteSize;

    // allocate send buffer and copy data
    std::unique_ptr<uint8_t[]> buffer         = std::make_unique<uint8_t[]>(rankInfoBufferSize);
    RankInfoBuffer&            rankInfoBuffer = *(RankInfoBuffer*)buffer.get();

    prepareQPsInfo(rankInfoBuffer);

    if (!m_coordClient->exchangeQpsInfo(m_commSize, rankInfoBuffer, rankInfoBufferSize, hcclRemoteDevices))
    {
        LOG_HCL_ERR(HCL, "failed to exchange QPs info with remote ranks");
        return hcclInternalError;
    }

    // update current rank info in the "remote device", just for completeness
    m_comm->m_remoteDevices[m_comm->m_rankInfo.header.hcclRank]->device = m_comm->m_rankInfo.device;
    m_comm->m_remoteDevices[m_comm->m_rankInfo.header.hcclRank]->remoteInfo =
        m_comm->m_rankInfo.remoteInfo[m_comm->m_rankInfo.header.hcclRank];

    updateRemoteDevices(hcclRemoteDevices);

    return hccl_device()->connectCommQps(*m_comm);
}

void hccl_communicator::initializeRanks(std::vector<RankInfoHeader>& hcclRankInfoHeaders,
                                        uint32_t                     commSize,
                                        bool                         isLoopbackModeOrNullSubmission)
{
    if (isLoopbackModeOrNullSubmission)
    {
        LOG_INFO(HCL, "Initializing {} 'ranks' for loopback/null-submit mode", commSize);
        for (unsigned i = 0; i < commSize; i++)
        {
            m_comm->AddNewRemoteDevice(i);
        }
    }
    else
    {
        m_comm->AddNewRemoteDevice(m_comm->m_rankInfo.header.hcclRank);
        m_comm->m_remoteDevices[m_comm->m_rankInfo.header.hcclRank]->header = m_comm->m_rankInfo.header;

        LOG_HCL_DEBUG(HCL,
                      "Add self to remote devices, device ({}) Rank ({}), ModuleID ({})",
                      hccl_device()->getHwModuleId(),
                      m_comm->m_rankInfo.header.hcclRank,
                      m_comm->m_rankInfo.header.hwModuleID);

        updateRemoteDevices(hcclRankInfoHeaders);
    }
}

hcclResult_t hccl_communicator::openConnections(bool isLoopbackModeOrNullSubmission)
{
    RET_ON_FAIL(m_comm->prepareAndValidateComm(isLoopbackModeOrNullSubmission));

    // fill ranks' caches to improve performance
    if (!isLoopbackModeOrNullSubmission)
    {
        m_comm->getInnerRanksExclusive();
        m_comm->getOuterRanksExclusive();
        m_comm->getConnectedRanks();
        m_comm->getAllOuterRanksExclusive();
    }

    RET_ON_FAIL(hccl_device()->IHclDevice::openQpToRemoteRanks(*m_comm));

    return hcclSuccess;
}

hcclResult_t hccl_communicator::initializeConnections(bool isLoopbackModeOrNullSubmission)
{
    hcclResult_t rc = hcclSuccess;
    if (isLoopbackModeOrNullSubmission)
    {
        const bool isLoopback = isLoopbackMode();
        if (isLoopback)
        {
            rc = openConnections(true);
            if (rc != hcclSuccess)
            {
                return rc;
            }
        }

        for (unsigned i = 0; i < m_commSize; i++)
        {
            m_comm->m_remoteDevices[i]->header = m_comm->m_rankInfo.header;
            // hwModuleID per box, in loopback box-size is 1, in null-submit box-size is 8
            if (isLoopback)
            {
                if (i == 0)
                {
                    m_comm->m_remoteDevices[i]->header.hwModuleID = hccl_device()->getHwModuleId();
                }
                else if (i == (unsigned)hccl_device()->getHwModuleId())
                {
                    m_comm->m_remoteDevices[i]->header.hwModuleID = 0;
                }
                else
                {
                    m_comm->m_remoteDevices[i]->header.hwModuleID = mod(i, m_comm->getScaleupGroupSize());
                }
            }
            else
            {
                m_comm->m_remoteDevices[i]->header.hwModuleID = i % m_boxSize;
            }
            m_comm->m_remoteDevices[i]->header.hcclRank = i;
            m_comm->m_remoteDevices[i]->device          = m_comm->m_rankInfo.device;
            m_comm->m_remoteDevices[i]->remoteInfo      = m_comm->m_rankInfo.remoteInfo[i];
            LOG_HCL_DEBUG(HCL,
                          "loopback set remote device({}) remote info ({},{},{})",
                          i,
                          m_comm->m_remoteDevices[i]->remoteInfo.gaudiNicQPs.qp[0].nic,
                          m_comm->m_remoteDevices[i]->remoteInfo.gaudiNicQPs.qp[1].nic,
                          m_comm->m_remoteDevices[i]->remoteInfo.gaudiNicQPs.qp[2].nic);
        }

        // @important: in null submit mode the above init code must be called before openQps,
        // since remote devices connection info is used during openQps
        if (!isLoopback)
        {
            rc = openConnections(true);
            if (rc != hcclSuccess)
            {
                return rc;
            }
        }
        rc = hccl_device()->connectCommQps(*m_comm);
    }
    else
    {
        rc = openConnections(false);
    }
    return rc;
}

hcclResult_t hccl_communicator::finalizeInitialization(bool isLoopbackModeOrNullSubmission)
{
    hcclResult_t rc = hcclSuccess;

    if (!isLoopbackModeOrNullSubmission)
    {
        g_hcclCordClient[*m_comm] = m_coordClient;

        // Sync ranks on bootstrap end to make sure all data was processed by all ranks data.
        LOG_HCL_DEBUG(HCL, "Sync using bootstrap - finalize comm init rank");
        if (!rendezvous())
        {
            LOG_HCL_ERR(HCL, "Hccl comm init rank finalization failed");
            return hcclInternalError;
        }
    }
    return rc;
}

// This function is going over all the ranks and does logical OR on the scaleout failure-mask that each
// one reports.
uint64_t hccl_communicator::getAccumulatedMask(const std::vector<RankInfoHeader>& RankInfoHeaders) const
{
    uint64_t                                       accumulatedMask = 0;
    std::unordered_map<uint64_t, std::vector<int>> mask2ranks;  // for logs only
    for (unsigned rank = 0; rank < m_commSize; rank++)
    {
        auto currMask = RankInfoHeaders[rank].failedScaleOutPortsMask;
        mask2ranks[currMask].push_back(rank);
        accumulatedMask |= currMask;
    }

    if (accumulatedMask != 0)
    {
        LOG_HCL_DEBUG(HCL, "Accumulated failed scaleout ports from all ranks: {:024b}", accumulatedMask);
        for (const auto& x : mask2ranks)
        {
            LOG_HCL_DEBUG(HCL,
                          "mask binary {:024b} ranks {}",
                          x.first,
                          fmt::join(x.second.begin(), x.second.end(), ","));
        }
    }

    return accumulatedMask;
}

hcclResult_t hccl_communicator::update_comm()
{
    static futex_t update_lock;

    LOG_HCL_TRACE(HCL, "{} started", (HCL_Comm)*m_comm);

    {
        locker_t locker(update_lock);  // serialize

        hccl_device()->deleteCommConnections(*m_comm);
        hccl_device()->invalidateCache(*m_comm);
        hccl_device().invalidateGraphCacheForComm(*m_comm);

        const uint64_t accumulatedGoodMask = ~m_lkdBadMask;
        m_comm->getCommConnectivity().updateScaleOutPortsMask(hccl_device()->getServerConnectivity(),
                                                              nics_mask_t(accumulatedGoodMask));

        RET_ON_FAIL(hccl_device()->IHclDevice::openQpToRemoteRanks(*m_comm));
    }

    RET_ON_FAIL(exchangeQpsData(false));

    if (!rendezvous())
    {
        return hcclInternalError;
    }

    LOG_HCL_TRACE(HCL, "{} completed", (HCL_Comm)*m_comm);

    return hcclSuccess;
}

hcclResult_t hccl_communicator::initialize(const internal_unique_id_t* internal_unique_id)
{
    hcclResult_t rc = hcclSuccess;

    std::vector<RankInfoHeader> hcclRankInfoHeaders;
    hcclRankInfoHeaders.resize(m_commSize);
    RankInfoHeader header {.hcclRank = m_rank};

    hccl_device()->getDeviceConfig().fillDeviceInfo(header);

    HCL_Comm hclCommId = hccl_device()->allocateNewComm();

    m_coordClient = std::make_shared<hlcp_client_t>(hclCommId, m_commSize, m_rank, internal_unique_id, (*this));

    // First Handshake
    rc = exchangeRankData(header, hcclRankInfoHeaders);
    if (rc != hcclSuccess) return rc;

    LOG_HCL_INFO(HCL_COORD, "Rank Communicator handshake1 done");

    // Param initialization after first handshake
    int rank      = m_rank;
    int commSize  = m_commSize;
    m_boxSize     = hcclRankInfoHeaders[m_rank].boxSize;
    int num_nodes = m_commSize / m_boxSize;
    if (num_nodes > 1 && !hccl_device()->isScaleOutAvailable())
    {
        LOG_HCL_ERR(HCL, "Scale-out is not available and communicator requires scale-out.");
        return hcclNoDeviceFound;
    }

    // Initialize HclConfig
    HclConfig config;
    if (!config.init(rank, commSize))
    {
        LOG_HCL_ERR(HCL, "Failed to initialize config with rank and commSize.");
        return hcclInternalError;
    }

    // create dynamic comm

    m_comm = &hccl_device()->getComm(hclCommId);
    m_comm->setUniqueID(internal_unique_id);

    // handle loopback mode and null submission
    bool isLoopbackModeOrNullSubmission = (isLoopbackMode() || GCFG_HCL_NULL_SUBMIT.value());

    int boxSize = m_boxSize;
    commSize    = m_commSize;
    rank        = m_rank;
    if (isLoopbackModeOrNullSubmission)
    {
        // workaround: in loopback mode we start with comm size = 1, but need to resize to 8
        m_comm->m_commSize = config.m_commSize;
        commSize           = config.m_commSize;
        boxSize            = hccl_device()->getServerDef().getDefaultBoxSize();
        rank               = m_comm->getMyRank();
    }

    m_commSize = commSize;
    // init dynamic comm
    // set dynamic comm size, mostly it is the hccl comm size
    // for G1 over host it is fixed to box size
    m_comm->init(m_commSize, rank, boxSize);

    rc = hccl_device()->onNewCommStart(hclCommId, m_commSize, config);
    if (rc != hcclSuccess)
    {
        LOG_HCL_ERR(HCL, "device onNewCommStart failed");
        return rc;
    }

    if (!isLoopbackModeOrNullSubmission)
    {
        RET_ON_FAIL(updateScaleoutPortMask(hcclRankInfoHeaders));
    }

    initializeRanks(hcclRankInfoHeaders, commSize, isLoopbackModeOrNullSubmission);

    std::vector<RemoteDeviceConnectionInfo> hcclRemoteDevices(m_commSize);

    LOG_HCL_INFO(HCL_COORD, "Rank initializeConnections start");
    rc = initializeConnections(isLoopbackModeOrNullSubmission);
    if (rc != hcclSuccess) return rc;

    // Second Handshake
    LOG_HCL_INFO(HCL_COORD, "Rank Communicator handshake2 start");
    rc = exchangeQpsData(isLoopbackModeOrNullSubmission);
    if (rc != hcclSuccess) return rc;
    LOG_HCL_INFO(HCL_COORD, "Rank Communicator handshake2 done");

    rc = finalizeInitialization(isLoopbackModeOrNullSubmission);
    if (rc != hcclSuccess) return rc;
    LOG_HCL_INFO(HCL_COORD, "Rank Communicator init done");

    // initial internal data structures for new communicator
    hccl_device().initComm(hclCommId);

    return rc;
}

void hccl_communicator::updateRemoteDevices(std::vector<RankInfoHeader>& hcclRankInfo)
{
    for (unsigned rank = 0; rank < m_commSize; rank++)
    {
        if (rank == (unsigned)m_rank) continue;

        LOG_HCL_DEBUG(HCL, "read rank data of rank {}", hcclRankInfo[rank].hcclRank);

        HCL_Rank rankHcl = rank;
        m_comm->AddNewRemoteDevice(rankHcl);
        LOG_HCL_TRACE(HCL,
                      "Add new device to remote devices, Rank ({}), ModuleID ({})",
                      hcclRankInfo[rankHcl].hcclRank,
                      hcclRankInfo[rankHcl].hwModuleID);

        m_comm->m_remoteDevices[rankHcl]->header = hcclRankInfo[rank];
    }
}

/**
 * @brief update all dynamic communicator remote devices with remote device connection info to current rank
 * @param device - the current device
 * @param hcclRemoteDevices - list of remote devices connection info to current device
 */
void hccl_communicator::updateRemoteDevices(std::vector<RemoteDeviceConnectionInfo>& hcclRemoteDevices)
{
    for (unsigned rank = 0; rank < m_commSize; rank++)
    {
        LOG_HCL_DEBUG(HCL, "({}) read rank data of hccl rank ({})", rank, hcclRemoteDevices[rank].header.hcclRank);

        // skip current rank
        if (rank == (unsigned)m_rank) continue;

        int rankHcl = rank;

        m_comm->AddNewRemoteDevice(rankHcl);
        LOG_HCL_TRACE(HCL,
                      "Rank({}) - Add new remote device, hcl Rank ({}), ModuleID ({})",
                      rank,
                      hcclRemoteDevices[rank].header.hcclRank,
                      hcclRemoteDevices[rank].header.hwModuleID);

        *(m_comm->m_remoteDevices[rankHcl]) = hcclRemoteDevices[rank];
    }
}

bool hccl_communicator::destroy()
{
    hccl_device()->destroyComm(*m_comm, false);

    return true;
}

void hccl_communicator::finalize(bool lockStreams)
{
    if (GCFG_HCL_NULL_SUBMIT.value()) return;

    std::vector<std::unique_ptr<std::lock_guard<std::mutex>>> guards;
    if (lockStreams)
    {
        for (size_t i = 0; i < m_comm->m_streamLatestLongSo.size(); i++)
        {
            // Use make_unique to create lock_guard objects
            // this is needed because std::vector requires the objects it stores to be copyable or moveable, and
            // std::lock_guard is neither
            guards.emplace_back(
                std::make_unique<std::lock_guard<std::mutex>>(hccl_device()->m_deviceController.getStreamLock(i)));
        }
    }

    for (size_t i = 0; i < m_comm->m_streamLatestLongSo.size(); i++)
    {
        LOG_HCL_DEBUG(HCL, "Wait on streamId: {}, targetVal: {}", i, m_comm->m_streamLatestLongSo[i]);
        hccl_device()->getScalManager().synchronizeStream(i, m_comm->m_streamLatestLongSo[i]);

        uint64_t scalExpectedTargetVal = 0;
        scal_completion_group_get_expected_ctr(hccl_device()->getScalManager().getCgHandle(i, true),
                                               &scalExpectedTargetVal);
        VERIFY_DFA_MSG(scalExpectedTargetVal >= m_comm->m_streamLatestLongSo[i],
                       "scalExpectedTargetVal was not updated correctly",
                       "scalExpectedTargetVal was not updated correctly, the actual value is greater than the expected "
                       "value. scalExpectedTargetVal: {}, m_comm->m_streamLatestLongSo[{}]: {}",
                       scalExpectedTargetVal,
                       i,
                       m_comm->m_streamLatestLongSo[i]);
    }
    LOG_HCL_DEBUG(HCL, "Finalized");
}

hccl_communicator::hccl_communicator(int rank, int comm_size) : m_rank(rank), m_commSize(comm_size)
{
    m_faultStopUntilApiCounters.resize(comm_size);
    m_faultStopUntilApiCounters.fill(ULLONG_MAX);
}

void hccl_communicator::incCollectiveCtr()
{
    m_comm->incCollectiveCtr();
    m_comm->incApiCollectivesCounter();
}

const uint64_t hccl_communicator::getCollectiveCtr() const
{
    return m_comm->getCollectiveCtr();
}

const uint64_t hccl_communicator::incSendCtr(const HCL_Rank peer)
{
    m_comm->incApiPreGroupEndSendCounter(peer);
    return m_comm->incSendCtr(peer);
}

const uint64_t hccl_communicator::getSendCtr(const HCL_Rank peer) const
{
    return m_comm->getSendCtr(peer);
}

const uint64_t hccl_communicator::incRecvCtr(const HCL_Rank peer)
{
    m_comm->incApiPreGroupEndRecvCounter(peer);
    return m_comm->incRecvCtr(peer);
}

const uint64_t hccl_communicator::getRecvCtr(const HCL_Rank peer) const
{
    return m_comm->getRecvCtr(peer);
}

const std::string hccl_communicator::getCommUniqueId()
{
    return m_comm->getCommUniqueId();
}

size_t hccl_communicator::getCommSize() const
{
    return m_commSize;
}

hcclResult_t hccl_communicator::comm_count(int* count)
{
    RETURN_ON_NULL_ARG(count);
    *count = static_cast<int>(m_commSize);
    return hcclSuccess;
}

hcclResult_t hccl_communicator::comm_user_rank(int* rank)
{
    RETURN_ON_NULL_ARG(rank);
    *rank = m_rank;
    LOG_HCL_DEBUG(HCL, "Communicator user rank is: {}", (int)*rank);
    return hcclSuccess;
}

int hccl_communicator::user_rank() const
{
    return m_rank;
}

hcclResult_t hccl_communicator::get_async_error(hcclResult_t* async_error)
{
    RETURN_ON_NULL_ARG(async_error);
    getDynamicComm()->getAsyncError(async_error);
    return hcclSuccess;
}

bool hccl_communicator::rendezvous()
{
    return m_coordClient->rendezvous();
}

HclDynamicCommunicator* hccl_communicator::getDynamicComm()
{
    return m_comm;
}

const HclDynamicCommunicator& hccl_communicator::getDynamicCommConst() const
{
    return *m_comm;
}

void hccl_communicator::faultTolerancePrepareMyCollectivesApiCounter(RankInfoBuffer& rankInfoBuffer) const
{
    const HCL_Comm commId                      = getDynamicCommConst();
    rankInfoBuffer.localInfo.header            = m_comm->m_rankInfo.header;
    rankInfoBuffer.localInfo.device            = m_comm->m_rankInfo.device;
    rankInfoBuffer.localInfo.header.apiCounter = m_comm->getApiCounters().collectivesCounter;
    HLFT_DBG("comm: {}, collectives API Counter={:#x}", commId, rankInfoBuffer.localInfo.header.apiCounter);
}

void hccl_communicator::buildMigrationAndCountersDataExchangeBuffer(remote_devices_t& remoteDevicesData,
                                                                    const bool        failOver)
{
    // Allocate buffers for exchange
    const uint32_t remoteSize = sizeof(RemoteInfo) * m_commSize;
    // total size to send - my_data + number of ranks * remote_data
    const uint32_t             rankInfoBufferSize = sizeof(LocalRankInfo) + remoteSize;
    std::unique_ptr<uint8_t[]> buffer             = std::make_unique<uint8_t[]>(rankInfoBufferSize);
    RankInfoBuffer*            rankInfoBuffer     = (RankInfoBuffer*)buffer.get();

    if (failOver)  // for failBack, not needed
    {
        prepareQPsInfo(*rankInfoBuffer);  // this has to be first, so it won't step over the below updates
    }
    faultTolerancePrepareMyCollectivesApiCounter(*rankInfoBuffer);  // maybe this should update the m_comm
    faultTolerancePrepareMySendRecvCounters(*rankInfoBuffer);

    // Exchange migration data with the other ranks in this comm
    if (!m_coordClient->exchangeMigrationData(m_commSize, *rankInfoBuffer, rankInfoBufferSize, remoteDevicesData))
    {
        HLFT_ERR("exchangeMigrationData - ranks discovery data failed");
        // handle error case - VERIFY abort possibly
    }
    updateRemoteDevices(remoteDevicesData);
}

void hccl_communicator::faultTolerancePrepareMySendRecvCounters(RankInfoBuffer& rankInfoBuffer) const
{
    const HCL_Comm commId = getDynamicCommConst();
    HLFT_DBG("comm: {}, Started", commId);

    const RankApiCounters& commCurrentApiCounters = m_comm->getApiCounters();
    for (HCL_Rank rank = 0; rank < m_commSize; rank++)
    {
        RemoteInfo&                remoteInfo = rankInfoBuffer.remoteInfo[rank];
        const SendRecvApiCounters& srCounters = commCurrentApiCounters.ranksSendRecv[rank];
        remoteInfo.counters.send              = srCounters.sendCounter;
        remoteInfo.counters.recv              = srCounters.recvCounter;
        if ((0 != remoteInfo.counters.send) || (0 != remoteInfo.counters.recv))
        {
            HLFT_TRC("comm: {}, Rank={}, send={}, recv={}",
                     commId,
                     rank,
                     remoteInfo.counters.send,
                     remoteInfo.counters.recv);
        }
    }
}

void hccl_communicator::faultToleranceCalcAllRanksMaxCounters(RankApiCounters&        maxRankApiCountersData,
                                                              const remote_devices_t& hcclRemoteDevices)
{
    const HCL_Comm         commId                 = getDynamicCommConst();
    const RankApiCounters& commCurrentApiCounters = m_comm->getApiCounters();
    const uint64_t         myCollectiveCtr        = getCollectiveCtr();
    HLFT_DBG("comm: {}, Started, my rank getCollectiveCtr()={:#x}", commId, myCollectiveCtr);
    commCurrentApiCounters.logDebug(commId, __FUNCTION__, "commCurrentApiCounters");
    maxRankApiCountersData.collectivesCounter = commCurrentApiCounters.collectivesCounter;

    // Init the max value to according to the target rank s/r counters
    // In case of group start/end, need to adjust these to group end counters since all submissions are done at group
    // end

    // iterate over remote devices data and set max collectives API counter and send/recv counters
    for (HCL_Rank rank = 0; rank < m_commSize; rank++)
    {
        const SendRecvApiCounters& srCounters = commCurrentApiCounters.ranksSendRecv[rank];
        HLFT_TRC("comm: {}, My rank to rank {}: send={}, recv={}",
                 commId,
                 rank,
                 srCounters.sendCounter,
                 srCounters.recvCounter);

        const RemoteDeviceConnectionInfo& remoteDevice = hcclRemoteDevices[rank];

        if (m_rank == rank)
        {
            HLFT_TRC("comm: {}, remote ranks info of my rank {}: collectives={:#x}, send={}, recv={}",
                     commId,
                     rank,
                     remoteDevice.header.apiCounter,
                     remoteDevice.remoteInfo.counters.send,
                     remoteDevice.remoteInfo.counters.recv);
            continue;
        }

        HLFT_TRC("comm: {}, Remote Rank {} info: collectives={:#x}, send={}, recv={}",
                 commId,
                 rank,
                 remoteDevice.header.apiCounter,
                 remoteDevice.remoteInfo.counters.send,
                 remoteDevice.remoteInfo.counters.recv);

        maxRankApiCountersData.collectivesCounter =
            std::max(maxRankApiCountersData.collectivesCounter, remoteDevice.header.apiCounter);

        // The max target value needs to be the max of my send counter to target and the recv counter from target to me
        // i.e. If i send 2 to j, then j will have 2 in his recv counter
        // The maximum value will be the max value of these ranks pair
        maxRankApiCountersData.ranksSendRecv[rank].sendCounter =
            std::max(srCounters.sendCounter, remoteDevice.remoteInfo.counters.recv);

        maxRankApiCountersData.ranksSendRecv[rank].recvCounter =
            std::max(srCounters.recvCounter, remoteDevice.remoteInfo.counters.send);
    }

    maxRankApiCountersData.logDebugCompare(commId, __FUNCTION__, "maxRankApiCountersData", commCurrentApiCounters);
}

void hccl_communicator::faultToleranceStopAllApis() const
{
    const HCL_Comm commId = getDynamicCommConst();
    HLFT_API_INF("---comm: {}, Performing Stop API", commId);
    // Notify user API thread to block further calls
    std::lock_guard<std::mutex> lk(g_faultsStopAllApiMutex);
    g_faultsCheckStopApi = true;  // Enable user thread to check conditions
    g_faultsStopAllApiCv.notify_all();
    HLFT_DBG("comm: {}, After notify", commId);
}

void hccl_communicator::faultToleranceResumeAllApis() const
{
    const HCL_Comm commId = getDynamicCommConst();
    HLFT_API_INF("---comm: {}, Performing Resume API", commId);
    // Notify user API thread to resume calls
    std::lock_guard<std::mutex> lk(g_faultsStopAllApiMutex);
    g_faultsCheckStopApi = false;  // Disable user thread from check stop condition after it will continue
    g_faultsStopAllApiCv.notify_all();
    HLFT_DBG("comm: {}, After notify", commId);
}

void hccl_communicator::faultToleranceStopCommAllApis()
{
    const HCL_Comm commId = getDynamicCommConst();
    HLFT_API_INF("---comm: {}, Performing Stop Comm API", commId);
    // Notify user API thread to block further calls
    std::lock_guard<std::mutex> lk(m_faultsStopCommApiMutex);
    m_faultStopUntilApiCounters.clear();
    m_faultsStopCommApiCv.notify_all();
    HLFT_DBG("comm: {}, After notify", commId);
}

void hccl_communicator::faultToleranceStopCommApisUntil(const RankApiCounters& stopUntil)
{
    const HCL_Comm commId = getDynamicCommConst();
    HLFT_API_INF("---comm: {}, Performing Stop Comm API until collective {:#x}", commId, stopUntil.collectivesCounter);
    std::lock_guard<std::mutex> lk(m_faultsStopCommApiMutex);
    m_faultStopUntilApiCounters = stopUntil;  // copies entire struct under mutex, so doesn't need to be atomic
    stopUntil.logDebug(commId, __FUNCTION__, "stopUntil");
    m_faultsStopCommApiCv.notify_all();
    HLFT_DBG("comm: {}, After notify", commId);
}

void hccl_communicator::faultToleranceResumeCommApis()
{
    const HCL_Comm commId = getDynamicCommConst();
    HLFT_API_INF("---comm: {}, Performing Resume API", commId);
    // Notify user API thread to resume calls
    std::lock_guard<std::mutex> lk(m_faultsStopCommApiMutex);
    m_faultStopUntilApiCounters.fill(ULLONG_MAX);
    m_faultsStopCommApiCv.notify_all();
}

static std::mutex s_guardStopApiMutex;  // Mutex to guard the stop API activation

void hccl_communicator::stopApis()
{
    const HCL_Comm commId = getDynamicCommConst();
    // Lock so only first thread at a time will do checks
    // other threads will wait until stop API CV was set before continuing
    HLFT_DBG("comm: {}, Before s_guardStopApiMutex lock, Stopping all API's", commId);
    std::lock_guard<std::mutex> lk(s_guardStopApiMutex);
    const uint32_t              commsToStopApi = g_faultsStopAllApi++;
    if (commsToStopApi == 0)
    {
        // Only first thread sets the CV
        faultToleranceStopAllApis();
    }
    else
    {
        HLFT_DBG("comm: {}, Stop All APIs flag already set", commId);
    }
    HLFT_DBG("comm: {}, Before comm API's stop", commId);
    faultToleranceStopCommAllApis();  // Will cause the user comm thread to stop unconditionally
}

void hccl_communicator::resumeUntil(const RankApiCounters& maxRankApiCountersData)
{
    const HCL_Comm commId = getDynamicCommConst();
    HLFT_INF("comm {}: Resume comm API's until:", commId);
    maxRankApiCountersData.logDebug(commId, __FUNCTION__, "maxRankApiCountersData");
    if (m_comm->getApiCounters().compareLessThanCounters(
            maxRankApiCountersData))  // Resume until if at least one of the counters is less than the max
    {
        faultToleranceStopCommApisUntil(maxRankApiCountersData);  // Will cause the user comm thread to resume until the
                                                                  // set of collectives and s/r counters are reached
    }
}

void hccl_communicator::resumeApis()
{
    const HCL_Comm commId = getDynamicCommConst();

    HLFT_INF("---comm: {}, Current g_faultsStopAllApi={}", commId, g_faultsStopAllApi.load());

    // Debug print only - should not print anything in normal operation
    // Check if the counter is already 0 before decrementing.
    if (g_faultsStopAllApi.load() == 0)
    {
        HLFT_ERR("comm: {}, Already 0 - something wrong, g_faultsCheckStopApi={}", commId, g_faultsCheckStopApi.load());
    }
    else  // must be positive
    {
        const uint32_t commsToResume = g_faultsStopAllApi--;
        if (commsToResume == 1)
        {
            // Only last comm thread unlocks the API's
            faultToleranceResumeAllApis();
        }
        else
        {
            HLFT_DBG("comm: {}, waiting for other comms to release all APIs", commId);
        }
        faultToleranceResumeCommApis();  // Resume this comm API's if any is blocked
    }
}

void hccl_communicator::mcNicStateChange(const NicState& nicState)
{
    const HCL_Comm commId = getDynamicCommConst();

    const bool portIsBad = !nicState.state;

    if (m_lkdBadMask[nicState.nic] == portIsBad)  // Note, in mask, true is bad. In NicState, true is good
    {
        HLFT_ERR("---comm: {}, handling rank: {}, nic: {}, state: {} lkdBadMask {:x}. State already set, skipping",
                 commId,
                 nicState.rank,
                 nicState.nic,
                 portIsBad ? "shutdown" : "up",
                 (uint64_t)m_lkdBadMask);
        return;
    }
    m_lkdBadMask[nicState.nic] = portIsBad;

    HLFT_DBG("---comm: {}, handling rank: {}, nic: {}, state: {} lkdBadMask {:x}",
             commId,
             nicState.rank,
             nicState.nic,
             portIsBad ? "shutdown" : "up",
             (uint64_t)m_lkdBadMask);

    if (portIsBad)  // shutdown
    {
        mcNicStateShutdown(nicState.nic);
    }
    else
    {
        mcNicStateUp(nicState.nic);
    }
}

void hccl_communicator::mcNicStateShutdown(const uint32_t logicalPort)
{
    m_comm->m_dfaData.failoverStart(logicalPort);
    const HCL_Comm commId = getDynamicCommConst();
    HLFT_INF("---comm: {}, Started", commId);

    // 1. Stop API - Stop general API's and stop comm specific API's
    stopApis();

    // 2. Perform short sleep here to let main user thread update the counters before its blocked.
    std::this_thread::sleep_for(std::chrono::milliseconds(GCFG_HCL_FAULT_TOLERANCE_DELAY_BEFORE_START.value()));

    m_comm->m_dfaData.updateFailoverStep(FaultToleranceState::FTcreateMigrationQPs);
    // 3. create_migration_qps();
    HLFT_INF("---comm: {}, createMigrationQps", commId);
    hccl_device()->createMigrationQps(commId, logicalPort);
    HLFT_INF("---comm: {}, createMigrationQps done", commId);
    // 4. Exchange migration data and counters
    // Allocate buffers for exchange
    remote_devices_t hcclRemoteDevices(m_commSize);
    buildMigrationAndCountersDataExchangeBuffer(hcclRemoteDevices, true);

    // 5. Calculate max collectives API and send/recv counters
    // Init buffer for max counters according to number of ranks
    RankApiCounters maxRankApiCountersData(m_commSize);

    faultToleranceCalcAllRanksMaxCounters(maxRankApiCountersData, hcclRemoteDevices);
    m_comm->m_dfaData.updateFailoverStep(FaultToleranceState::FTmoveToRts, &maxRankApiCountersData);
    // Continue migration qp setup after exchange
    // 6. setup_rtr_part(recv_b);
    // 7. hlcp_client->commRendezvous();
    // 8. setup_rts_part();
    HLFT_INF("---comm: {}, updateMigrationQpsToRts", commId);
    hccl_device()->updateMigrationQpsToRts(commId);

    m_comm->m_dfaData.updateFailoverStep(FaultToleranceState::FTwaitMaxCounters);
    // 9. Resume comm API until
    HLFT_INF("---comm: {}, updateMigrationQpsToRts done; Resume comm API's", commId);
    resumeUntil(maxRankApiCountersData);

    // 10. Wait loop with synchronize event on target long SO and target collective API counter
    faultTolerancePollUntilApiReach(maxRankApiCountersData);
    HLFT_INF("---comm: {}, deleteMigrationQPs", commId);
    hccl_device()->deleteMigrationQPs(commId);

    m_comm->m_dfaData.updateFailoverStep(FaultToleranceState::FTcommUpdate);
    // 11. Comm ReInit
    HLFT_INF("---comm: {}, going to update_comm", commId);
    VERIFY_DFA(hcclSuccess == update_comm());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // dummy sleep for testing
    // 12. Resume All API's
    resumeApis();
    HLFT_INF("---comm: {}, Failover done", commId);
    m_comm->m_dfaData.failoverEnd();
}

void hccl_communicator::mcNicStateUp(const uint32_t /*logicalPort*/)
{
    m_comm->m_dfaData.failbackStart();
    const HCL_Comm commId = getDynamicCommConst();
    HLFT_INF("---comm: {}, Started", commId);

    // 1. Stop API - Stop general API's and stop comm specific API's
    stopApis();

    // 2. Perform short sleep here to let main user thread update the counters before its blocked.
    std::this_thread::sleep_for(std::chrono::milliseconds(GCFG_HCL_FAULT_TOLERANCE_DELAY_BEFORE_START.value()));

    // 3. Exchange counters
    // Allocate buffers for exchange
    remote_devices_t hcclRemoteDevices(m_commSize);
    buildMigrationAndCountersDataExchangeBuffer(hcclRemoteDevices, false);

    // 4. Calculate max collectives API and send/recv counters
    // Init buffer for max counters according to number of ranks
    RankApiCounters maxRankApiCountersData(m_commSize);

    faultToleranceCalcAllRanksMaxCounters(maxRankApiCountersData, hcclRemoteDevices);
    m_comm->m_dfaData.updateFailbackStep(FaultToleranceState::FTwaitMaxCounters, &maxRankApiCountersData);
    // 5. Resume comm API until (will cause the user comm thread to resume until this counter)
    HLFT_INF("---comm: {}, Resume comm API's", commId);
    resumeUntil(maxRankApiCountersData);

    // 6. Wait loop with synchronize event on target long SO and target collective API counter
    faultTolerancePollUntilApiReach(maxRankApiCountersData);

    m_comm->m_dfaData.updateFailbackStep(FaultToleranceState::FTcommUpdate);
    // 7. Comm ReInit
    HLFT_INF("---comm: {}, going to update_comm", commId);
    VERIFY_DFA(hcclSuccess == update_comm());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // dummy sleep for testing
    // 8. Resume All API's
    resumeApis();
    HLFT_INF("---comm: {}, Failback done", commId);
    m_comm->m_dfaData.failbackEnd();
}

// Called to check if to stop comm specific collectives API's during fault tolerance handling from hccl.cpp and
// hccl_wrapper.cpp
void hccl_communicator::checkFaultToleranceStopCommCollApiUntil()
{
    const HCL_Comm commId = getDynamicCommConst();
    HLFT_DBG("Comm Stop API check, comm={}", commId);
    std::unique_lock<std::mutex> lk(m_faultsStopCommApiMutex);
    HLFT_DBG("Before CV wait, getCollectiveCtr()={:#x}, m_comm->getApiCounters().collectivesCounter={:#x}, "
             "ApiCounters.collectivesCounter={:#x}",
             getCollectiveCtr(),
             m_comm->getApiCounters().collectivesCounter,
             m_faultStopUntilApiCounters.collectivesCounter);
    m_faultsStopCommApiCv.wait(lk, [this] {
        return (m_comm->getApiCounters().collectivesCounter < m_faultStopUntilApiCounters.collectivesCounter);
    }); /* Block if current comm API counter is equal or greater to target counter  */
    HLFT_INF("---comm: {} After CV wait, User API thread is unblocked, reached "
             "m_comm->getApiCounters().collectivesCounter={:#x}",
             commId,
             m_comm->getApiCounters().collectivesCounter);
}

void hccl_communicator::faultTolerancePollUntilApiReach(const RankApiCounters& maxRankApiCountersData)
{
    const HCL_Comm commId = getDynamicCommConst();

    FaultToleranceTargetCounters myCommCounters = m_comm->getFaultToleranceTargetCounters();  // read first time
    while (true)
    {
        HLFT_INF("---comm: {} sleep before next check of API counters", commId);
        std::this_thread::sleep_for(std::chrono::milliseconds(
            GCFG_HCL_FAULT_TOLERANCE_COMM_POLL_INTERVAL.value()));  // sleep to allow user threads to complete APIs

        // Read Long SO stream counters and their matching API counters
        myCommCounters = m_comm->getFaultToleranceTargetCounters();
        myCommCounters.rankApiCountersData.logDebug(commId, __FUNCTION__, "current.rankApiCountersData");
        // debug print
        {
            for (size_t i = 0; i < myCommCounters.streamLongSo.size(); i++)
            {
                HLFT_DBG("comm {}:, current streamLongSo[{}]={:#x}", commId, i, myCommCounters.streamLongSo[i]);
            }
        }

        if (myCommCounters.rankApiCountersData.compareEqualCounters(maxRankApiCountersData))
        {
            HLFT_INF("---comm: {} Reached target collectiveCounter={:#x}",
                     commId,
                     myCommCounters.rankApiCountersData.collectivesCounter);
            break;
        }
        else
        {
            HLFT_DBG("comm {}: current collectiveCounter={:#x} didn't hit target collectiveCounter={:#x} yet",
                     commId,
                     myCommCounters.rankApiCountersData.collectivesCounter,
                     maxRankApiCountersData.collectivesCounter);
        }
    }

    // Synchronize on all target long SO streams for this comm
    // Do we need to lock here?
    for (size_t i = 0; i < myCommCounters.streamLongSo.size(); i++)
    {
        HLFT_INF("---comm: {} Wait on streamId {}, targetVal={}", commId, i, myCommCounters.streamLongSo[i]);
        hccl_device()->getScalManager().synchronizeStream(i, myCommCounters.streamLongSo[i]);
    }
}

// Called to check if to stop comm specific s/r submissions in  HCL API group end, during fault tolerance handling
void hccl_communicator::checkFaultToleranceStopCommSendRecvApiUntil()
{
    const HCL_Comm commId = getDynamicCommConst();
    HLFT_DBG("Comm Stop API check, comm={}", commId);
    std::unique_lock<std::mutex> lk(m_faultsStopCommApiMutex);
    HLFT_DBG("Before CV wait, comm={}", commId);

    m_faultStopUntilApiCounters.logDebug(commId, __FUNCTION__, "m_faultStopUntilApiCounters");  // Target counters
    m_comm->getApiCounters().logDebug(commId, __FUNCTION__, "getApiCounters()");                // current counters

    m_faultsStopCommApiCv.wait(lk, [this] {
        return (m_comm->getApiCounters().compareLessThanCounters(m_faultStopUntilApiCounters));
    }); /* Block if current comm S/R API counters are ALL equal or greater than target counters  */
    HLFT_INF("---comm: {} After CV wait, User API thread is unblocked", commId);

    m_comm->getApiCounters().logDebug(commId, __FUNCTION__, "After getApiCounters()");
}
