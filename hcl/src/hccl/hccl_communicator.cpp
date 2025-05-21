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
#include "ibverbs/hcl_ibverbs.h"
#include "hcl_bits.h"  // for nics_mask_t

#define RET_ON_FAIL(func)                                                                                              \
    {                                                                                                                  \
        hcclResult_t __rc = func;                                                                                      \
        if (__rc != hcclSuccess)                                                                                       \
        {                                                                                                              \
            LOG_HCL_ERR(HCL, #func " failed: {}", __rc);                                                               \
            return __rc;                                                                                               \
        }                                                                                                              \
    }

std::unordered_map<HCL_Comm, spHcclCoordinatorClient> g_hcclCordClient;

hcclResult_t hccl_communicator::exchangeRankData(RankInfoHeader&              header,
                                                 std::vector<RankInfoHeader>& hcclRankInfoHeaders)
{
    hcclResult_t rc = hcclSuccess;
    if (!GCFG_HCL_NULL_SUBMIT.value())
    {
        if (!m_coordClient->exchangeRankInfo(m_commSize, header, hcclRankInfoHeaders))
        {
            LOG_HCL_ERR(HCL, "Comm {} Handshake1 with remote ranks failed", (const HCL_Comm)(*m_comm));
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
                    "Comm {} No common scaleout ports between ranks. FailedMask {:024b} External mask {:024b}",
                    (const HCL_Comm)(*m_comm),
                    (uint64_t)accumulatedFailedMask,
                    (uint64_t)hccl_device()->getServerConnectivity().getExternalPortsMaskGlbl());
        return hcclInternalError;
    }

    const uint64_t accumulatedGoodMask = ~accumulatedFailedMask;
    m_comm->getCommConnectivity().updateScaleOutPortsMask(hccl_device()->getServerConnectivity(),
                                                          nics_mask_t(accumulatedGoodMask));
    m_failedPorts = accumulatedFailedMask;

    return hcclSuccess;
}

hcclResult_t hccl_communicator::exchangeQpsData(bool isLoopbackModeOrNullSubmission)
{
    LOG_HCL_TRACE(HCL, "Started, comm={}", (const HCL_Comm)(*m_comm));
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
        LOG_HCL_ERR(HCL, "Comm {}, failed to exchange QPs info with remote ranks", (const HCL_Comm)(*m_comm));
        return hcclInternalError;
    }

    // update current rank info in the "remote device", just for completeness
    m_comm->m_remoteDevices[m_comm->m_rankInfo.header.hcclRank]->device = m_comm->m_rankInfo.device;
    m_comm->m_remoteDevices[m_comm->m_rankInfo.header.hcclRank]->remoteInfo =
        m_comm->m_rankInfo.remoteInfo[m_comm->m_rankInfo.header.hcclRank];

    updateRemoteDevicesConnections(hcclRemoteDevices);

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

        updateRemoteDevicesHeader(hcclRankInfoHeaders);
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
        LOG_HCL_DEBUG(HCL, "Comm {} Sync using bootstrap - finalize comm init rank", (const HCL_Comm)(*m_comm));
        if (!rendezvous())
        {
            LOG_HCL_ERR(HCL, "Comm {} Hccl comm init rank finalization failed", (const HCL_Comm)(*m_comm));
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
    const CommIds commIds = getCommIds();
    LOG_HCL_TRACE(HCL, COMM_ID_FMT "Started", commIds.commId, commIds.commIdPort);

    hccl_device()->deleteCommConnections(*m_comm);
    hccl_device()->invalidateCache(*m_comm);
    hccl_device().invalidateGraphCacheForComm(*m_comm);

    const uint64_t accumulatedGoodMask = ~m_failedPorts;
    m_comm->getCommConnectivity().updateScaleOutPortsMask(hccl_device()->getServerConnectivity(),
                                                          nics_mask_t(accumulatedGoodMask));
    RET_ON_FAIL(hccl_device()->IHclDevice::openQpToRemoteRanks(*m_comm));

    RET_ON_FAIL(exchangeQpsData(false));
    LOG_HCL_TRACE(HCL, COMM_ID_FMT "Before rendezvous", commIds.commId, commIds.commIdPort);

    if (!rendezvous(true))
    {
        return hcclInternalError;
    }

    LOG_HCL_TRACE(HCL, COMM_ID_FMT "Completed", commIds.commId, commIds.commIdPort);

    return hcclSuccess;
}

hcclResult_t hccl_communicator::initialize(const internal_unique_id_t* internal_unique_id)
{
    hcclResult_t rc = hcclSuccess;

    std::vector<RankInfoHeader> hcclRankInfoHeaders;
    hcclRankInfoHeaders.resize(m_commSize);
    RankInfoHeader header {.hcclRank = m_rank};

    hccl_device()->getDeviceConfig().fillDeviceInfo(header);

    const HCL_Comm hclCommId = hccl_device()->allocateNewComm();
    g_ibv.on_comm_init(hclCommId);

    const size_t qpCommSize = isLoopbackMode() ? GCFG_LOOPBACK_COMMUNICATOR_SIZE.value() : m_commSize;

    hccl_device()->setQpManagersForComm(hclCommId, qpCommSize);

    m_coordClient = std::make_shared<hlcp_client_t>(hclCommId, m_commSize, m_rank, internal_unique_id, (*this));

    // First Handshake
    rc = exchangeRankData(header, hcclRankInfoHeaders);
    if (rc != hcclSuccess) return rc;

    LOG_HCL_INFO(HCL_COORD, "Comm {} Rank Communicator handshake1 done", hclCommId);

    // Param initialization after first handshake
    int rank      = m_rank;
    int commSize  = m_commSize;
    m_boxSize     = hcclRankInfoHeaders[m_rank].boxSize;
    int num_nodes = m_commSize / m_boxSize;
    if (num_nodes > 1 && !hccl_device()->isScaleOutAvailable())
    {
        LOG_HCL_ERR(HCL, "Comm {} Scale-out is not available and communicator requires scale-out.", hclCommId);
        return hcclNoDeviceFound;
    }

    // Initialize HclConfig
    HclConfig config;
    if (!config.init(rank, commSize))
    {
        LOG_HCL_ERR(HCL, "Comm {} Failed to initialize config with rank and commSize.", hclCommId);
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
        LOG_HCL_ERR(HCL, "Comm {}, device onNewCommStart failed", hclCommId);
        return rc;
    }

    if (!isLoopbackModeOrNullSubmission)
    {
        RET_ON_FAIL(updateScaleoutPortMask(hcclRankInfoHeaders));
    }

    initializeRanks(hcclRankInfoHeaders, commSize, isLoopbackModeOrNullSubmission);

    std::vector<RemoteDeviceConnectionInfo> hcclRemoteDevices(m_commSize);

    LOG_HCL_INFO(HCL_COORD, "Comm {} Rank initializeConnections start", hclCommId);
    rc = initializeConnections(isLoopbackModeOrNullSubmission);
    if (rc != hcclSuccess) return rc;

    // Second Handshake
    LOG_HCL_INFO(HCL_COORD, "Comm {} Rank Communicator handshake2 start", hclCommId);
    rc = exchangeQpsData(isLoopbackModeOrNullSubmission);
    if (rc != hcclSuccess) return rc;
    LOG_HCL_INFO(HCL_COORD, "Comm {} Rank Communicator handshake2 done", hclCommId);

    rc = finalizeInitialization(isLoopbackModeOrNullSubmission);
    if (rc != hcclSuccess) return rc;
    LOG_HCL_INFO(HCL_COORD, "Comm {} Rank Communicator init done", hclCommId);

    hccl_device()->faultToleranceCommInit(hclCommId);

    // initial internal data structures for new communicator
    hccl_device().initComm(hclCommId);

    return rc;
}

void hccl_communicator::updateRemoteDevicesHeader(const std::vector<RankInfoHeader>& hcclRankInfo)
{
    for (unsigned rank = 0; rank < m_commSize; rank++)
    {
        if (rank == (unsigned)m_rank) continue;

        LOG_HCL_DEBUG(HCL,
                      "Comm {}, read rank data of rank {}",
                      (const HCL_Comm)(*m_comm),
                      hcclRankInfo[rank].hcclRank);

        HCL_Rank rankHcl = rank;
        m_comm->AddNewRemoteDevice(rankHcl);
        LOG_HCL_TRACE(HCL,
                      "Comm {}, Add new device to remote devices, Rank ({}), ModuleID ({})",
                      (const HCL_Comm)(*m_comm),
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
void hccl_communicator::updateRemoteDevicesConnections(const std::vector<RemoteDeviceConnectionInfo>& hcclRemoteDevices)
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

void hccl_communicator::updateRemoteCounters(const remote_counters_ranks_t& remoteRanksInfo)
{
    const CommIds commIds = getCommIds();
    HLFT_COMM_TRC("Called", commIds);
    for (unsigned rank = 0; rank < m_commSize; rank++)
    {
        // skip current rank
        if (rank == (unsigned)m_rank) continue;

        LOG_HCL_DEBUG(HCL,
                      "Rank({}) - Copying counters data from remote rank ({}), collectivesCounter=(0x{:x}), "
                      "counters.send=(0x{:x}), counters.recv=(0x{:x})",
                      rank,
                      remoteRanksInfo[rank].header.hcclRank,
                      remoteRanksInfo[rank].header.collectivesCounter,
                      remoteRanksInfo[rank].remoteInfo.counters.send,
                      remoteRanksInfo[rank].remoteInfo.counters.recv);

        // Update only the counters in the dynamic communicator data structure
        (m_comm->m_remoteDevices[rank])->remoteInfo.counters.send = remoteRanksInfo[rank].remoteInfo.counters.send;
        (m_comm->m_remoteDevices[rank])->remoteInfo.counters.recv = remoteRanksInfo[rank].remoteInfo.counters.recv;
        (m_comm->m_remoteDevices[rank])->header.apiCounter        = remoteRanksInfo[rank].header.collectivesCounter;
    }
}

bool hccl_communicator::destroy()
{
    HCL_Comm commId = *m_comm;

    hccl_device()->destroyComm(commId, false);
    g_ibv.on_comm_destroy(commId);

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
    return m_comm->incSendCtr(peer);
}

const uint64_t hccl_communicator::getSendCtr(const HCL_Rank peer) const
{
    return m_comm->getSendCtr(peer);
}

const uint64_t hccl_communicator::incRecvCtr(const HCL_Rank peer)
{
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
    LOG_HCL_DEBUG(HCL, "Comm {} Communicator user rank is: {}", (const HCL_Comm)(*m_comm), (int)*rank);
    return hcclSuccess;
}

int hccl_communicator::user_rank() const
{
    return m_rank;
}

hcclResult_t hccl_communicator::get_async_error(hcclResult_t* async_error)
{
    RETURN_ON_NULL_ARG(async_error);
    m_async_error_message.clear();
    getDynamicComm()->getAsyncError(async_error, m_async_error_message);
    if (!m_async_error_message.empty())
    {
        setGlobalAsyncErrorMessage(m_async_error_message);
    }
    return hcclSuccess;
}

const char* hccl_communicator::get_async_error_message()
{
    return m_async_error_message.empty() ? nullptr : m_async_error_message.c_str();
}

bool hccl_communicator::rendezvous(bool migration_finished)
{
    return m_coordClient->rendezvous(migration_finished);
}

HclDynamicCommunicator* hccl_communicator::getDynamicComm()
{
    return m_comm;
}

void hccl_communicator::faultTolerancePrepareMyCollectivesApiCounter(RanksExchangeBuffers& ranksExchangeInfo) const
{
    RankInfoBuffer& rankInfoBuffer(ranksExchangeInfo.getRankInfoBuffer());
    const CommIds   commIds                    = getCommIds();
    rankInfoBuffer.localInfo.header            = m_comm->m_rankInfo.header;
    rankInfoBuffer.localInfo.device            = m_comm->m_rankInfo.device;
    rankInfoBuffer.localInfo.header.apiCounter = m_comm->getApiCounters().collectivesCounter;
    ranksExchangeInfo.getRemoteSyncCountersInfoBuffer().localInfo.collectivesCounter =
        m_comm->getApiCounters().collectivesCounter;
    ;
    HLFT_COMM_DBG("Collectives API Counter={:#x}", commIds, rankInfoBuffer.localInfo.header.apiCounter);
}

void hccl_communicator::initRanksExchangeBuffers(RanksExchangeBuffers& ranksExchangeBuffers) const
{
    const CommIds commIds = getCommIds();

    // Allocate buffers for exchange
    const uint32_t remoteSize = sizeof(RemoteInfo) * m_commSize;
    // Total size to send - my_data + number of ranks * remote_data
    const uint32_t rankInfoBufferSize       = sizeof(LocalRankInfo) + remoteSize;
    ranksExchangeBuffers.myBufferSize       = rankInfoBufferSize;
    ranksExchangeBuffers.rankInfoSendBuffer = std::make_unique<uint8_t[]>(rankInfoBufferSize);
    std::fill_n(ranksExchangeBuffers.rankInfoSendBuffer.get(), rankInfoBufferSize, 0);
    HLFT_COMM_DBG("remoteSize={}, rankInfoBufferSize={}", commIds, remoteSize, rankInfoBufferSize);

    const uint32_t remoteSyncCountersSize =
        sizeof(FtSyncCountersRemoteInfo) * m_commSize;  // s/r counters from all ranks
    const uint32_t mySyncCountersRankData           = sizeof(FtSyncCountersInfoHeader) + remoteSyncCountersSize;
    ranksExchangeBuffers.mySyncCountersBufferSize   = mySyncCountersRankData;
    ranksExchangeBuffers.rankSyncCountersSendBuffer = std::make_unique<uint8_t[]>(mySyncCountersRankData);
    std::fill_n(ranksExchangeBuffers.rankSyncCountersSendBuffer.get(), mySyncCountersRankData, 0);
    HLFT_COMM_DBG("remoteSyncCountersSize={}, mySyncCountersRankData={}",
                  commIds,
                  remoteSyncCountersSize,
                  mySyncCountersRankData);

    ranksExchangeBuffers.hcclRemoteDevices.resize(m_commSize);   // receive buffers
    ranksExchangeBuffers.remoteSyncCounters.resize(m_commSize);  // receive buffers

    ranksExchangeBuffers.getRemoteSyncCountersInfoBuffer().localInfo          = FtSyncCountersInfoHeader();
    ranksExchangeBuffers.getRemoteSyncCountersInfoBuffer().localInfo.hcclRank = m_rank;
}

void hccl_communicator::updateMigrationAndCountersDataAndExchangeBuffers(RanksExchangeBuffers& ranksExchangeInfo,
                                                                         const bool            failOver)
{
    const CommIds commIds = getCommIds();
    HLFT_COMM_DBG("Started, failOver={}", commIds, failOver);

    RankInfoBuffer&   rankInfoBuffer(ranksExchangeInfo.getRankInfoBuffer());
    remote_devices_t& remoteDevicesData(ranksExchangeInfo.hcclRemoteDevices);

    if (failOver)  // for failBack, not needed
    {
        prepareQPsInfo(rankInfoBuffer);  // this has to be first, so it won't step over the below updates
    }
    faultTolerancePrepareMyCollectivesApiCounter(ranksExchangeInfo);
    faultTolerancePrepareMySendRecvCounters(ranksExchangeInfo);

    // Exchange migration data with the other ranks in this comm
    if (!m_coordClient->exchangeMigrationData(m_commSize,
                                              rankInfoBuffer,
                                              ranksExchangeInfo.myBufferSize,
                                              remoteDevicesData))
    {
        HLFT_COMM_ERR("exchangeMigrationData - ranks exchange data failed", commIds);
        // Handle error case - VERIFY abort possibly
    }

    updateRemoteDevicesConnections(remoteDevicesData);
}

bool hccl_communicator::updateReachedTargetAndExchangeBuffers(const bool            reachedEqualCounters,
                                                              RanksExchangeBuffers& ranksExchangeInfo)
{
    const CommIds commIds = getCommIds();

    FtRanksInfoBuffer& remoteSyncCountersInfoBuffer =
        ranksExchangeInfo.getRemoteSyncCountersInfoBuffer();  // Send buffer

    remoteSyncCountersInfoBuffer.localInfo.myCountersReached = reachedEqualCounters;
    remoteSyncCountersInfoBuffer.localInfo.myCountersVersion++;
    HLFT_COMM_DBG("Started, reachedEqualCounters={}, hcclRank={}, myCountersVersion={}, collectivesCounter=(0x{:x})",
                  commIds,
                  reachedEqualCounters,
                  remoteSyncCountersInfoBuffer.localInfo.hcclRank,
                  remoteSyncCountersInfoBuffer.localInfo.myCountersVersion,
                  remoteSyncCountersInfoBuffer.localInfo.collectivesCounter);

    for (HCL_Rank rank = 0; rank < m_commSize; rank++)
    {
        const uint64_t sendCounter = remoteSyncCountersInfoBuffer.remoteInfo[rank].counters.send;
        const uint64_t recvCounter = remoteSyncCountersInfoBuffer.remoteInfo[rank].counters.recv;
        if ((0 != sendCounter) || (0 != recvCounter))
        {
            HLFT_COMM_TRC("Rank={}, sendCounter=(0x{:x}), recvCounter=(0x{:x})",
                          commIds,
                          rank,
                          sendCounter,
                          recvCounter);
        }
    }

    // Exchange data with the other ranks in this comm
    bool                     allRanksDone = false;
    remote_counters_ranks_t& remoteRanksInfo(ranksExchangeInfo.remoteSyncCounters);  // receive buffers
    if (!m_coordClient->exchangeCountersData(m_commSize,
                                             remoteSyncCountersInfoBuffer,
                                             ranksExchangeInfo.mySyncCountersBufferSize,
                                             allRanksDone,
                                             remoteRanksInfo))
    {
        HLFT_COMM_ERR("exchangeCountersData - ranks exchange data failed", commIds);
        // Handle error case - VERIFY abort possibly, since xchg_counters_data will abort with error before ?
    }
    HLFT_COMM_INF("allRanksDone={}", commIds, allRanksDone);

    // Print remote ranks data we received
    for (HCL_Rank rank = 0; rank < m_commSize; rank++)
    {
        const RemoteDeviceSyncCountersInfo& remoteRankSyncData = remoteRanksInfo[rank];

        HLFT_COMM_TRC("Remote Rank {} info: collectivesCounter=(0x{:x}), myCountersReached={}, myCountersVersion={}",
                      commIds,
                      rank,
                      remoteRankSyncData.header.collectivesCounter,
                      remoteRankSyncData.header.myCountersReached,
                      remoteRankSyncData.header.myCountersVersion);
    }

    // Update our internal data from the received data
    updateRemoteCounters(remoteRanksInfo);

    return allRanksDone;
}

void hccl_communicator::faultTolerancePrepareMySendRecvCounters(RanksExchangeBuffers& ranksExchangeInfo) const
{
    const CommIds commIds = getCommIds();
    HLFT_COMM_DBG("Started", commIds);

    RankInfoBuffer&        rankInfoBuffer               = ranksExchangeInfo.getRankInfoBuffer();
    FtRanksInfoBuffer&     remoteSyncCountersInfoBuffer = ranksExchangeInfo.getRemoteSyncCountersInfoBuffer();
    const RankApiCounters& commCurrentApiCounters       = m_comm->getApiCounters();
    for (HCL_Rank rank = 0; rank < m_commSize; rank++)
    {
        const SendRecvApiCounters& srCounters                       = commCurrentApiCounters.ranksSendRecv[rank];
        RemoteInfo&                remoteInfo                       = rankInfoBuffer.remoteInfo[rank];
        remoteInfo.counters.send                                    = srCounters.sendCounter;
        remoteInfo.counters.recv                                    = srCounters.recvCounter;
        remoteSyncCountersInfoBuffer.remoteInfo[rank].counters.send = srCounters.sendCounter;
        remoteSyncCountersInfoBuffer.remoteInfo[rank].counters.recv = srCounters.recvCounter;
        if ((0 != srCounters.sendCounter) || (0 != srCounters.recvCounter))
        {
            HLFT_COMM_TRC("Rank={}, send=(0x{:x}), recv=(0x{:x})",
                          commIds,
                          rank,
                          srCounters.sendCounter,
                          srCounters.recvCounter);
        }
    }
}

void hccl_communicator::faultToleranceCalcAllRanksMaxCounters(const remote_counters_ranks_t& remoteRanksInfo,
                                                              RankApiCounters&               maxRankApiCountersData)
{
    const CommIds          commIds                = getCommIds();
    const RankApiCounters& commCurrentApiCounters = m_comm->getApiCounters();
    const uint64_t         myCollectiveCtr        = getCollectiveCtr();
    HLFT_COMM_DBG("Started, my rank getCollectiveCtr()={:#x}", commIds, myCollectiveCtr);
    commCurrentApiCounters.logDebug(commIds, __FUNCTION__, "commCurrentApiCounters");
    maxRankApiCountersData.collectivesCounter = commCurrentApiCounters.collectivesCounter;

    // Iterate over remote devices data and set max collectives API counter and send/recv counters
    // In case of group start/end, these will be the group end counters since all submissions are done at group end
    for (HCL_Rank rank = 0; rank < m_commSize; rank++)
    {
        const SendRecvApiCounters& srCounters = commCurrentApiCounters.ranksSendRecv[rank];
        HLFT_COMM_TRC("My rank to rank {}: send={:#x}, recv={:#x}",
                      commIds,
                      rank,
                      srCounters.sendCounter,
                      srCounters.recvCounter);

        const RemoteDeviceSyncCountersInfo& remoteRankInfo = remoteRanksInfo[rank];

        if (m_rank == rank)
        {
            HLFT_COMM_TRC("Ignoring remote rank info of my rank {}: collectives=(0x{:x}), send=(0x{:x}), recv=(0x{:x})",
                          commIds,
                          rank,
                          remoteRankInfo.header.collectivesCounter,
                          remoteRankInfo.remoteInfo.counters.send,
                          remoteRankInfo.remoteInfo.counters.recv);
            continue;
        }

        HLFT_COMM_TRC("Processing remote Rank {} info: collectives=(0x{:x}), send=(0x{:x}), recv=(0x{:x})",
                      commIds,
                      rank,
                      remoteRankInfo.header.collectivesCounter,
                      remoteRankInfo.remoteInfo.counters.send,
                      remoteRankInfo.remoteInfo.counters.recv);

        maxRankApiCountersData.collectivesCounter =
            std::max(maxRankApiCountersData.collectivesCounter, remoteRankInfo.header.collectivesCounter);

        // The max target value needs to be the max of my send counter to target and the recv counter from target to me
        // i.e. If i sends 2 to j, then j will have 2 in its recv counter
        // The maximum value will be the max value of these ranks pair
        maxRankApiCountersData.ranksSendRecv[rank].sendCounter =
            std::max(srCounters.sendCounter, remoteRankInfo.remoteInfo.counters.recv);

        maxRankApiCountersData.ranksSendRecv[rank].recvCounter =
            std::max(srCounters.recvCounter, remoteRankInfo.remoteInfo.counters.send);
    }

    maxRankApiCountersData.logDebugCompare(commIds, __FUNCTION__, "maxRankApiCountersData", commCurrentApiCounters);
}

void hccl_communicator::faultToleranceStopAllApis() const
{
    const CommIds commIds = getCommIds();
    HLFT_API_COMM_HDR_INF("Performing Stop API", commIds);
    // Notify user API thread to block further calls
    {
        std::lock_guard<std::mutex> lk(g_faultsStopAllApiMutex);
        g_faultsCheckStopApi = true;  // Enable user thread to check conditions
    }
    g_faultsStopAllApiCv.notify_all();
    HLFT_COMM_DBG("After notify", commIds);
}

void hccl_communicator::faultToleranceResumeAllApis() const
{
    const CommIds commIds = getCommIds();
    HLFT_API_COMM_HDR_INF("Performing Resume API", commIds);
    // Notify user API thread to resume calls
    {
        std::lock_guard<std::mutex> lk(g_faultsStopAllApiMutex);
        g_faultsCheckStopApi = false;  // Disable user thread from check stop condition after it will continue
    }
    g_faultsStopAllApiCv.notify_all();
    HLFT_COMM_DBG("After notify", commIds);
}

void hccl_communicator::faultToleranceStopCommAllApis()
{
    const CommIds commIds = getCommIds();
    HLFT_API_COMM_HDR_INF("Performing Stop Comm API", commIds);
    {
        // Notify user API thread to block further calls
        std::lock_guard<std::mutex> lk(m_faultsStopCommApiMutex);
        m_faultStopUntilApiCounters.clear();
    }
    m_faultsStopCommApiCv.notify_all();
    HLFT_COMM_DBG("After notify", commIds);
}

void hccl_communicator::faultToleranceContinueCommApisUntil(const RankApiCounters& stopUntil)
{
    const CommIds commIds = getCommIds();
    HLFT_API_COMM_HDR_INF("Performing Stop Comm API until collective {:#x}", commIds, stopUntil.collectivesCounter);
    {
        std::lock_guard<std::mutex> lk(m_faultsStopCommApiMutex);
        m_faultStopUntilApiCounters = stopUntil;  // copies entire struct under mutex, so doesn't need to be atomic
    }
    stopUntil.logDebug(commIds, __FUNCTION__, "stopUntil");
    m_faultsStopCommApiCv.notify_all();
    hccl_device()->faultToleranceNotifyGroupApis();  // group end API can be shared for multiple comms -- check if to
                                                     // make it wake once
    HLFT_COMM_DBG("After notify", commIds);
}

void hccl_communicator::faultToleranceResumeCommApis()
{
    const CommIds commIds = getCommIds();
    HLFT_API_COMM_HDR_INF("Performing Resume API", commIds);
    // Notify user API thread to resume calls
    {
        std::lock_guard<std::mutex> lk(m_faultsStopCommApiMutex);
        m_faultStopUntilApiCounters.fill(ULLONG_MAX);
    }
    m_faultsStopCommApiCv.notify_all();
    hccl_device()->faultToleranceNotifyGroupApis();  // group end API can be shared for multiple comms -- check if to
                                                     // make it wake once
}

static std::mutex s_guardStopApiMutex;  // Mutex to guard the stop API activation

void hccl_communicator::stopApis()
{
    const CommIds commIds = getCommIds();
    // Lock so only first thread at a time will do checks
    // other threads will wait until stop API CV was set before continuing
    HLFT_COMM_DBG("Before s_guardStopApiMutex lock, Stopping all API's", commIds);
    std::lock_guard<std::mutex> lk(s_guardStopApiMutex);
    const uint32_t              commsToStopApi = g_faultsStopAllApi++;
    if (commsToStopApi == 0)
    {
        // Only first thread sets the CV
        faultToleranceStopAllApis();
    }
    else
    {
        HLFT_COMM_DBG("Stop All APIs flag already set", commIds);
    }
    HLFT_COMM_DBG("Before comm API's stop", commIds);
    faultToleranceStopCommAllApis();                 // Will cause the user comm thread to stop unconditionally
    hccl_device()->faultToleranceNotifyGroupApis();  // Group end API can be shared for multiple comms -- check if to
                                                     // make it wake once
}

void hccl_communicator::resumeUntil(const RankApiCounters& maxRankApiCountersData)
{
    const CommIds commIds = getCommIds();
    HLFT_COMM_INF("Resume comm API's until:", commIds);
    maxRankApiCountersData.logDebug(commIds, __FUNCTION__, "maxRankApiCountersData");
    m_comm->getApiCounters().logDebug(commIds, __FUNCTION__, "m_comm->getApiCounters()");
    if (m_comm->getApiCounters().compareLessThanCounters(
            maxRankApiCountersData))  // Resume until if at least one of the counters is less than the max
    {
        faultToleranceContinueCommApisUntil(
            maxRankApiCountersData);  // Will cause the user comm thread to resume until the
                                      // set of collectives and s/r counters are reached
    }
}

void hccl_communicator::resumeApis()
{
    const CommIds commIds = getCommIds();

    HLFT_COMM_HDR_INF("Current g_faultsStopAllApi={}", commIds, g_faultsStopAllApi.load());

    // Debug print only - should not print anything in normal operation
    // Check if the counter is already 0 before decrementing.
    if (g_faultsStopAllApi.load() == 0)
    {
        HLFT_COMM_ERR("Already 0 - something wrong, g_faultsCheckStopApi={}", commIds, g_faultsCheckStopApi.load());
    }
    else  // must be positive
    {
        const uint32_t commsToResume = g_faultsStopAllApi--;
        HLFT_COMM_HDR_INF("commsToResume={}", commIds, commsToResume);
        if (commsToResume == 1)
        {
            // Only last comm thread unlocks the API's
            faultToleranceResumeAllApis();
        }
        else
        {
            HLFT_COMM_DBG("Waiting for other comms to release all APIs", commIds);
        }
        faultToleranceResumeCommApis();  // Resume this comm API's if any is blocked
    }
}

void hccl_communicator::mcNicStateChange(const NicState& nicState)
{
    const CommIds commIds = getCommIds();

    const bool portIsBad = !nicState.state;

    if (m_failedPorts[nicState.nic] == portIsBad)  // Note, in mask, true is bad. In NicState, true is good
    {
        HLFT_COMM_HDR_ERR("handling rank: {}, nic: {}, state: {} lkdBadMask {:x}. State already set, skipping",
                          commIds,
                          nicState.rank,
                          nicState.nic,
                          portIsBad ? "shutdown" : "up",
                          (uint64_t)m_failedPorts);
        return;
    }
    m_failedPorts[nicState.nic] = portIsBad;

    HLFT_COMM_HDR_DBG("handling rank: {}, nic: {}, state: {} lkdBadMask {:x}",
                      commIds,
                      nicState.rank,
                      nicState.nic,
                      portIsBad ? "shutdown" : "up",
                      (uint64_t)m_failedPorts);

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
    const CommIds commIds = getCommIds();
    HLFT_COMM_HDR_INF("Started", commIds);

    // 1. Stop API - Stop general API's and stop comm specific API's
    stopApis();

    // 2. Perform short sleep here to let main user thread update the counters before its blocked.
    std::this_thread::sleep_for(std::chrono::milliseconds(GCFG_HCL_FAULT_TOLERANCE_DELAY_BEFORE_START.value()));

    // 3. create_migration_qps();
    m_comm->m_dfaData.updateFailoverStep(FaultToleranceState::FTcreateMigrationQPs);
    HLFT_COMM_HDR_INF("createMigrationQps", commIds);
    hccl_device()->createMigrationQps(commIds.commId, logicalPort);
    HLFT_COMM_HDR_INF("createMigrationQps done", commIds);

    // 4. Exchange migration data and counters
    RanksExchangeBuffers ranksExchangeBuffers;
    initRanksExchangeBuffers(ranksExchangeBuffers);  // Init buffers for exchange
    updateMigrationAndCountersDataAndExchangeBuffers(ranksExchangeBuffers, true);
    copyDevicesCountersToSyncCounters(ranksExchangeBuffers.hcclRemoteDevices, ranksExchangeBuffers.remoteSyncCounters);

    // 5. Calculate max collectives API and send/recv counters
    // Init buffer for max counters according to number of ranks
    RankApiCounters maxRankApiCountersData(m_commSize);
    faultToleranceCalcAllRanksMaxCounters(ranksExchangeBuffers.remoteSyncCounters, maxRankApiCountersData);
    m_comm->m_dfaData.updateFailoverStep(FaultToleranceState::FTmoveToRts, &maxRankApiCountersData);

    // Continue migration qp setup after exchange
    // 6. setup_rtr_part(recv_b);
    // 7. hlcp_client->commRendezvous();
    // 8. setup_rts_part();
    HLFT_COMM_HDR_INF("updateMigrationQpsToRts", commIds);
    hccl_device()->updateMigrationQpsToRts(commIds);

    // 9. Resume comm API until
    m_comm->m_dfaData.updateFailoverStep(FaultToleranceState::FTwaitMaxCounters);
    HLFT_COMM_HDR_INF("updateMigrationQpsToRts done; Resume comm API's", commIds);
    resumeUntil(maxRankApiCountersData);

    HLFT_COMM_HDR_INF("After resume API's", commIds);

    while (true)
    {
        // 10. Wait loop to reach or exceed target collective API counter
        const bool reachedEqualCounters = faultTolerancePollUntilApiReach(maxRankApiCountersData,
                                                                          ranksExchangeBuffers.hcclRemoteDevices,
                                                                          ranksExchangeBuffers.remoteSyncCounters);

        HLFT_COMM_HDR_INF("faultTolerancePollUntilApiReach cycle done; Notify other ranks we reached target, "
                          "reachedEqualCounters={}",
                          commIds,
                          reachedEqualCounters);

        const bool allRanksDone = updateReachedTargetAndExchangeBuffers(
            reachedEqualCounters,
            ranksExchangeBuffers);  // sends data to server. Server may either send us in return new counters from
                                    // all ranks or wills end all done
        if (allRanksDone)
        {
            break;  // All ranks reached target, no more exchanges needed, time to sync streams
        }

        // We need to update our max counters according to the new counters we received from the server
        faultToleranceCalcAllRanksMaxCounters(ranksExchangeBuffers.remoteSyncCounters, maxRankApiCountersData);
        faultTolerancePrepareMyCollectivesApiCounter(ranksExchangeBuffers);
        faultTolerancePrepareMySendRecvCounters(ranksExchangeBuffers);
        resumeUntil(maxRankApiCountersData);

        HLFT_COMM_HDR_INF("sleep before next loop check", commIds);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // sleep to allow user threads to complete APIs
    }

    m_comm->m_dfaData.updateFailoverStep(FaultToleranceState::FTreachedTarget);

    // 10. Synchronize long SO
    HLFT_COMM_HDR_INF("Exchange done; sync streams", commIds);
    faultToleranceStreamsSync(maxRankApiCountersData);

    // 11. Delete migration QPs
    HLFT_COMM_HDR_INF("deleteMigrationQPs", commIds);
    hccl_device()->deleteMigrationQPs(commIds.commId);

    // 12. Comm ReInit
    m_comm->m_dfaData.updateFailoverStep(FaultToleranceState::FTcommUpdate);
    HLFT_COMM_HDR_INF("going to update_comm", commIds);
    VERIFY_DFA(hcclSuccess == update_comm());

    // 13. Resume All API's
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // dummy sleep for testing
    resumeApis();

    // 14. Failover done
    HLFT_COMM_HDR_INF("Failover done", commIds);
    m_comm->m_dfaData.failoverEnd();
}

void hccl_communicator::mcNicStateUp(const uint32_t /*logicalPort*/)
{
    m_comm->m_dfaData.failbackStart();
    const CommIds commIds = getCommIds();
    HLFT_COMM_HDR_INF("Started", commIds);

    // 1. Stop API - Stop general API's and stop comm specific API's
    stopApis();

    // 2. Perform short sleep here to let main user thread update the counters before its blocked.
    std::this_thread::sleep_for(std::chrono::milliseconds(GCFG_HCL_FAULT_TOLERANCE_DELAY_BEFORE_START.value()));

    // 3. Exchange counters
    RanksExchangeBuffers ranksExchangeBuffers;
    initRanksExchangeBuffers(ranksExchangeBuffers);  // Init buffers for exchange
    updateMigrationAndCountersDataAndExchangeBuffers(ranksExchangeBuffers, false);
    copyDevicesCountersToSyncCounters(ranksExchangeBuffers.hcclRemoteDevices, ranksExchangeBuffers.remoteSyncCounters);

    // 4. Calculate max collectives API and send/recv counters
    // Init buffer for max counters according to number of ranks
    RankApiCounters maxRankApiCountersData(m_commSize);

    faultToleranceCalcAllRanksMaxCounters(ranksExchangeBuffers.remoteSyncCounters, maxRankApiCountersData);
    m_comm->m_dfaData.updateFailbackStep(FaultToleranceState::FTwaitMaxCounters, &maxRankApiCountersData);

    // 5. Resume comm API until (will cause the user comm thread to resume until this counter)
    HLFT_COMM_HDR_INF("Resume comm API's", commIds);
    resumeUntil(maxRankApiCountersData);

    HLFT_COMM_HDR_INF("After resume API's", commIds);

    while (true)
    {
        // 6. Wait loop to reach or exceed target collective API counter
        const bool reachedEqualCounters = faultTolerancePollUntilApiReach(maxRankApiCountersData,
                                                                          ranksExchangeBuffers.hcclRemoteDevices,
                                                                          ranksExchangeBuffers.remoteSyncCounters);

        HLFT_COMM_HDR_INF("faultTolerancePollUntilApiReach cycle done; Notify other ranks if we reached target, "
                          "reachedEqualCounters={}",
                          commIds,
                          reachedEqualCounters);

        const bool allRanksDone = updateReachedTargetAndExchangeBuffers(
            reachedEqualCounters,
            ranksExchangeBuffers);  // sends data to server. Server may either send us in return new max counters from
                                    // all ranks or wills end all done
        if (allRanksDone)
        {
            break;  // All ranks reached target, no more exchanges needed, time to sync streams
        }

        // We need to update our max counters according to the new counters we received from the server
        faultToleranceCalcAllRanksMaxCounters(ranksExchangeBuffers.remoteSyncCounters, maxRankApiCountersData);
        faultTolerancePrepareMyCollectivesApiCounter(ranksExchangeBuffers);
        faultTolerancePrepareMySendRecvCounters(ranksExchangeBuffers);
        resumeUntil(maxRankApiCountersData);

        HLFT_COMM_HDR_INF("Sleep before next loop check", commIds);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // sleep to allow user threads to complete APIs
    }

    // 7. Synchronize long SO
    m_comm->m_dfaData.updateFailoverStep(FaultToleranceState::FTreachedTarget);
    HLFT_COMM_HDR_INF("Exchange done; sync streams", commIds);
    faultToleranceStreamsSync(maxRankApiCountersData);

    // 8. Comm ReInit
    m_comm->m_dfaData.updateFailbackStep(FaultToleranceState::FTcommUpdate);
    HLFT_COMM_HDR_INF("Going to update_comm", commIds);
    VERIFY_DFA(hcclSuccess == update_comm());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // dummy sleep for testing

    // 9. Resume All API's
    resumeApis();

    // 10. Failback done
    HLFT_COMM_HDR_INF("Failback done", commIds);
    m_comm->m_dfaData.failbackEnd();
}

// Called to check if to stop comm specific collectives API's during fault tolerance handling from hccl.cpp and
// hccl_wrapper.cpp
void hccl_communicator::checkFaultToleranceStopCommCollApiUntil()
{
    const CommIds commIds = getCommIds();
    HLFT_COMM_DBG("Comm Stop API check, ", commIds);
    std::unique_lock<std::mutex> lk(m_faultsStopCommApiMutex);
    HLFT_DBG("Before CV wait, getCollectiveCtr()={:#x}, m_comm->getApiCounters().collectivesCounter={:#x}, "
             "ApiCounters.collectivesCounter={:#x}",
             getCollectiveCtr(),
             m_comm->getApiCounters().collectivesCounter,
             m_faultStopUntilApiCounters.collectivesCounter);
    m_faultsStopCommApiCv.wait(lk, [this] {
        return (m_comm->getApiCounters().collectivesCounter < m_faultStopUntilApiCounters.collectivesCounter);
    }); /* Block if current comm API counter is equal or greater to target counter  */
    HLFT_COMM_HDR_INF("After CV wait, User API thread is unblocked, reached "
                      "m_comm->getApiCounters().collectivesCounter={:#x}",
                      commIds,
                      m_comm->getApiCounters().collectivesCounter);
}

void hccl_communicator::copyDevicesCountersToSyncCounters(const remote_devices_t&  remoteDevices,
                                                          remote_counters_ranks_t& remoteRanksInfo)
{
    const CommIds commIds = getCommIds();
    HLFT_COMM_DBG("Copying one time from remoteDevices (size {})  to remoteRanksInfo (size {})",
                  commIds,
                  remoteDevices.size(),
                  remoteRanksInfo.size());

    for (HCL_Rank rank = 0; rank < remoteDevices.size(); rank++)
    {
        const RemoteDeviceConnectionInfo& remoteDevice   = remoteDevices[rank];
        RemoteDeviceSyncCountersInfo&     remoteRankInfo = remoteRanksInfo[rank];
        remoteRankInfo.header.collectivesCounter         = remoteDevice.header.apiCounter;
        remoteRankInfo.remoteInfo.counters.send          = remoteDevice.remoteInfo.counters.send;
        remoteRankInfo.remoteInfo.counters.recv          = remoteDevice.remoteInfo.counters.recv;
    }
}

bool hccl_communicator::faultTolerancePollUntilApiReach(const RankApiCounters&         maxRankApiCountersData,
                                                        const remote_devices_t&        remoteDevices,
                                                        const remote_counters_ranks_t& remoteRanksInfo)
{
    const CommIds commIds = getCommIds();

    FaultToleranceTargetCounters myCommCounters       = m_comm->getFaultToleranceTargetCounters();  // read first time
    bool                         reachedEqualCounters = false;
    while (true)
    {
        HLFT_COMM_HDR_INF("Sleep before next check of API counters, remoteDevices.size={}, remoteRanksInfo.size={}",
                          commIds,
                          remoteDevices.size(),
                          remoteRanksInfo.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(
            GCFG_HCL_FAULT_TOLERANCE_COMM_POLL_INTERVAL.value()));  // sleep to allow user threads to complete APIs

        HLFT_COMM_TRC("After sleep", commIds);

        // Read Long SO stream counters and their matching API counters
        myCommCounters = m_comm->getFaultToleranceTargetCounters();
        myCommCounters.rankApiCountersData.logDebug(commIds, __FUNCTION__, "current.rankApiCountersData");

        const uint64_t myCollectivesCounter = myCommCounters.rankApiCountersData.collectivesCounter;

        if (myCommCounters.rankApiCountersData.compareEqualCounters(maxRankApiCountersData))
        {
            HLFT_COMM_HDR_INF("Reached target collectiveCounter=(0x{:x})", commIds, myCollectivesCounter);
            // Although we reached the target, we need to verify that all ranks have the same collective & S/R counters
            bool crossRanksMatch = true;
            for (HCL_Rank remoteRank = 0; remoteRank < myCommCounters.rankApiCountersData.ranksSendRecv.size();
                 remoteRank++)
            {
                if (m_rank == remoteRank)
                {
                    continue;
                }
                const uint64_t remoteApiCounter = remoteRanksInfo[remoteRank].header.collectivesCounter;
                if (myCollectivesCounter != remoteApiCounter)
                {
                    crossRanksMatch = false;
                    HLFT_COMM_HDR_INF(
                        "myCollectivesCounter=(0x{:x}) doesn't match remote rank {} remoteApiCounter=(0x{:x})",
                        commIds,
                        myCollectivesCounter,
                        remoteRank,
                        remoteApiCounter);
                    break;
                }
                const uint64_t mySendCount = myCommCounters.rankApiCountersData.ranksSendRecv[remoteRank].sendCounter;
                const uint64_t remoteRecvCounter = remoteRanksInfo[remoteRank].remoteInfo.counters.recv;

                if (mySendCount != remoteRecvCounter)
                {
                    crossRanksMatch = false;
                    HLFT_COMM_HDR_INF("mySendCount=(0x{:x}) doesn't match remote rank {} remoteRecvCounter=(0x{:x})",
                                      commIds,
                                      mySendCount,
                                      remoteRank,
                                      remoteRecvCounter);
                    break;
                }

                const uint64_t myRecvCount = myCommCounters.rankApiCountersData.ranksSendRecv[remoteRank].recvCounter;
                const uint64_t remoteSendCounter = remoteRanksInfo[remoteRank].remoteInfo.counters.send;
                if (myRecvCount != remoteSendCounter)
                {
                    crossRanksMatch = false;
                    HLFT_COMM_HDR_INF("myRecvCount=(0x{:x}) doesn't match remote rank {} remoteSendCounter=(0x{:x})",
                                      commIds,
                                      myRecvCount,
                                      remoteRank,
                                      remoteSendCounter);
                    break;
                }
            }
            reachedEqualCounters = crossRanksMatch;
            break;
        }
        else if (!myCommCounters.rankApiCountersData.compareLessThanCounters(
                     maxRankApiCountersData))  // API counters are greater than target counters
        {
            HLFT_COMM_HDR_INF(
                "myCollectivesCounter=(0x{:x}) is greater or equal than target collectiveCounter=(0x{:x})",
                commIds,
                myCollectivesCounter,
                maxRankApiCountersData.collectivesCounter);
            break;
        }
        else
        {
            // debug print
            for (size_t i = 0; i < myCommCounters.streamLongSo.size(); i++)
            {
                HLFT_COMM_DBG("current streamLongSo[{}]=(0x{:x})", commIds, i, myCommCounters.streamLongSo[i]);
            }
            HLFT_COMM_DBG("myCollectivesCounter=(0x{:x}) didn't hit target collectiveCounter=(0x{:x}) yet",
                          commIds,
                          myCollectivesCounter,
                          maxRankApiCountersData.collectivesCounter);
        }
    }

    return reachedEqualCounters;
}

void hccl_communicator::faultToleranceStreamsSync([[maybe_unused]] const RankApiCounters& maxRankApiCountersData)
{
    const CommIds commIds = getCommIds();
    HLFT_COMM_HDR_INF("sync streams", commIds);

    // Read Long SO stream counters and their matching API counters
    const FaultToleranceTargetCounters myCommCounters = m_comm->getFaultToleranceTargetCounters();
    myCommCounters.rankApiCountersData.logDebug(commIds, __FUNCTION__, "myCommCounters.rankApiCountersData");

    // Synchronize on all target long SO streams for this comm
    // Do we need to lock here?
    for (size_t i = 0; i < myCommCounters.streamLongSo.size(); i++)
    {
        uint64_t currentLongSo = hccl_device()->getScalManager().getCurrentLongSoValue(i);

        HLFT_COMM_HDR_INF("Wait on streamId {}, targetVal={}, current {}",
                          commIds,
                          i,
                          myCommCounters.streamLongSo[i],
                          currentLongSo);
        hccl_device()->getScalManager().synchronizeStream(i, myCommCounters.streamLongSo[i]);
    }
}

// Checks if this comm is relevant for the group API (it is not after update_comm). Return if the current comm S/R &
// collectives API counters are less than the target counters.
const TargetCountersCheckResult hccl_communicator::checkFaultToleranceStopCommSendRecvApiUntil() const
{
    const CommIds commIds = getCommIds();
    HLFT_COMM_DBG("Comm Stop API check", commIds);
    m_faultStopUntilApiCounters.logDebug(commIds, __FUNCTION__, "m_faultStopUntilApiCounters");  // Target counters
    m_comm->getApiCounters().logDebug(commIds, __FUNCTION__, "getApiCounters()");                // current counters

    // This comm is not relevant for group call when the update_comm stage has been reached for it. This is done by
    // checking if the target counter is ULONG_MAX value. Its enough to check only the collectives counter, since S/R
    // counters should also be ULLONG_MAX by fill() function
    if (ULLONG_MAX == m_faultStopUntilApiCounters.collectivesCounter)
    {
        LOG_HCL_TRACE(HCL,
                      COMM_ID_FMT,
                      "Fault tolerance stop until API set to be ignored",
                      commIds.commId,
                      commIds.commIdPort);
        return TargetCountersCheckResult::FT_TARGET_COUNTERS_CHECK_RESULT_IGNORE;
    }
    const bool result = m_comm->getApiCounters().compareLessThanCounters(m_faultStopUntilApiCounters);
    LOG_HCL_TRACE(HCL, COMM_ID_FMT "result={}", commIds.commId, commIds.commIdPort, result);
    return result ? TargetCountersCheckResult::FT_TARGET_COUNTERS_CHECK_RESULT_LESS_THAN
                  : TargetCountersCheckResult::FT_TARGET_COUNTERS_CHECK_RESULT_GREATER_EQUAL;
}
