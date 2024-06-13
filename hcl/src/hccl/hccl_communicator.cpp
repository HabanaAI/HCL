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
#include <algorithm>                     // for max, find
#include <array>                         // for array
#include <cstddef>                       // for size_t, NULL
#include <cstdint>                       // for uint64_t, uint8_t, uin...
#include <cstring>                       // for memset
#include <sstream>                       // for basic_ostream::operator<<
#include <unordered_map>                 // for unordered_map, unorder...
#include "hccl_helpers.h"                // for RETURN_ON_SYNAPSE_ERROR
#include "hccl_internal_defs.h"          // for hcclHandle, HOST_BUFF_INC
#include "hccl_types.h"                  // for hcclSuccess, hcclResult_t
#include "hccl_device.h"
#include "hcl_api_types.h"               // for HCL_Comm, eHCLReduceSc...
#include "hcl_config.h"                  // for HclConfig, HclDeviceCo...
#include "hcl_dynamic_communicator.h"    // for HclDynamicCommunicator
#include "hcl_global_conf.h"             // for GlobalConfBool, GCFG_H...
#include "hcl_types.h"                   // for RankInfo, HclConfigType
#include "hcl_utils.h"                   // for LOG_HCL_ERR, VERIFY
#include "interfaces/hcl_idevice.h"      // for IHclDevice
#include "libfabric/mr_mapping.h"        // for MRMapping
#include "libfabric/hl_ofi.h"            // for ofi_t
#include "libfabric/hl_ofi_component.h"  // for ofi_component_t
#include "hcl_log_manager.h"             // for LOG_ERR, LOG_DEBUG
#include "ofi_communicator.h"            // for ofi_communicator
#include "interfaces/hccl_syn_stream.h"  // for synapse_stream_wrap
#include "synapse_api.h"                 // for synHostFree, synEventC...
#include "synapse_api_types.h"           // for InternalStreamHandle
#include "synapse_common_types.h"        // for synSuccess, synStatus
#include "hcl_math_utils.h"
#include "platform/gaudi2/hcl_device.h"  // for HclDeviceGaudi2

std::unordered_map<HCL_Comm, spHcclCoordinatorClient> g_hcclCordClient;

hcclResult_t hccl_communicator::firstHandShakeAtInit(RankInfoHeader&              header,
                                                     std::vector<RankInfoHeader>& hcclRankInfoHeaders)
{
    hcclResult_t rc = hcclSuccess;
    if (!GCFG_HCL_NULL_SUBMIT.value())
    {
        if (!m_coordClient->commInitHandshake1(m_commSize, header, hcclRankInfoHeaders))
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
            hcclRankInfoHeaders[i].boxSize = hccl_device()->getHal()->getDefaultBoxSize();
        }
    }
    return rc;
}

void hccl_communicator::buildSecondHandShakeRemoteInfoBuffer(RankInfoBuffer& rankInfoBuffer)
{
    rankInfoBuffer.localInfo.header = m_comm->m_rankInfo.header;
    rankInfoBuffer.localInfo.device = m_comm->m_rankInfo.device;

    memcpy(rankInfoBuffer.remoteInfo, m_comm->m_rankInfo.remoteInfo.data(), sizeof(RemoteInfo) * m_commSize);
}

hcclResult_t hccl_communicator::secondHandShakeAtInit(std::vector<RemoteDeviceConnectionInfo>& hcclRemoteDevices,
                                                      bool isLoopbackModeOrNullSubmission)
{
    hcclResult_t rc = hcclSuccess;

    if (isLoopbackModeOrNullSubmission)
    {
        return rc;
    }

    // RemoteInfo data size
    const uint32_t remote_size = sizeof(RemoteInfo) * m_commSize;
    // total size to send
    const uint32_t rankInfoBufferSize = sizeof(LocalRankInfo) + remote_size;

    // allocate send buffer and copy data
    std::unique_ptr<uint8_t[]> buffer         = std::make_unique<uint8_t[]>(rankInfoBufferSize);
    RankInfoBuffer*            rankInfoBuffer = (RankInfoBuffer*)buffer.get();

    buildSecondHandShakeRemoteInfoBuffer(*rankInfoBuffer);

    LOG_HCL_INFO(HCL_COORD, "Rank handshake2 sending to coordinator");
    if (!m_coordClient->commInitHandshake2(m_commSize, (void*)rankInfoBuffer, rankInfoBufferSize, hcclRemoteDevices))
    {
        LOG_HCL_ERR(HCL, "Handshake2 - ranks discovery data failed");
        return hcclInternalError;
    }

    // update current rank info in the "remote device", just for completeness
    m_comm->m_remoteDevices[m_comm->m_rankInfo.header.hcclRank]->device = m_comm->m_rankInfo.device;
    m_comm->m_remoteDevices[m_comm->m_rankInfo.header.hcclRank]->remoteInfo =
        m_comm->m_rankInfo.remoteInfo[m_comm->m_rankInfo.header.hcclRank];

    updateRemoteDevices(hcclRemoteDevices);

    LOG_HCL_INFO(HCL_COORD, "Rank handshake2 received data from coordinator, update device QPs");
    hccl_device()->updateQps(*m_comm);

    return rc;
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
        m_comm->m_remoteDevices[m_comm->m_rankInfo.header.hcclRank]->header =
            m_comm->m_rankInfo.header;

        LOG_HCL_DEBUG(HCL,
                      "Add self to remote devices, device ({}) Rank ({}), ModuleID ({})",
                      hccl_device()->m_deviceId,
                      m_comm->m_rankInfo.header.hcclRank,
                      m_comm->m_rankInfo.header.hwModuleID);

        updateRemoteDevices(hcclRankInfoHeaders);
    }
}

hcclResult_t hccl_communicator::openConnections(bool isLoopbackModeOrNullSubmission)
{
    hcclResult_t ret = m_comm->prepareAndValidateComm(isLoopbackModeOrNullSubmission);
    if (ret != hcclSuccess)
    {
        return ret;
    }

    // fill ranks' caches to improve performance
    if (!isLoopbackModeOrNullSubmission)
    {
        m_comm->getInnerRanksExclusive();
        m_comm->getOuterRanksExclusive();
        m_comm->getConnectedRanks();
    }

    ret = hccl_device()->IHclDevice::openQps(*m_comm);
    if (ret != hcclSuccess)
    {
        LOG_HCL_ERR(HCL, "FAILED - openQps({})", ret);
    }

    return ret;
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
                    m_comm->m_remoteDevices[i]->header.hwModuleID = i;
                }
            }
            else
            {
                m_comm->m_remoteDevices[i]->header.hwModuleID = i % m_boxSize;
            }
            m_comm->m_remoteDevices[i]->header.hcclRank       = i;
            m_comm->m_remoteDevices[i]->device = m_comm->m_rankInfo.device;
            m_comm->m_remoteDevices[i]->remoteInfo =
                m_comm->m_rankInfo.remoteInfo[i];
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
        hccl_device()->updateQps(*m_comm);
    }
    else
    {
        rc = openConnections(false);
    }
    return rc;
}

hcclResult_t hccl_communicator::finalizeInitialization(std::vector<RemoteDeviceConnectionInfo>& hcclRemoteDevices,
                                                       bool isLoopbackModeOrNullSubmission)
{
    hcclResult_t rc = hcclSuccess;

    if (!isLoopbackModeOrNullSubmission)
    {
        g_hcclCordClient[*m_comm] = m_coordClient;

        // Sync ranks on bootstrap end to make sure all data was processed by all ranks data.
        LOG_HCL_DEBUG(HCL, "Sync using bootstrap - finalize comm init rank");
        if (!syncBetweenRanks())
        {
            LOG_HCL_ERR(HCL, "Hccl comm init rank finalization failed");
            return hcclInternalError;
        }
    }
    return rc;
}

hcclResult_t hccl_communicator::initialize(const internal_unique_id_t* internal_unique_id)
{
    LOG_HCL_INFO(HCL_COORD, "Rank Communicator init start");
    hcclResult_t rc = hcclSuccess;

    std::vector<RankInfoHeader> hcclRankInfoHeaders;
    hcclRankInfoHeaders.resize(m_commSize);
    RankInfoHeader header {.hcclRank = m_rank};

    hccl_device()->getDeviceConfig().fillDeviceInfo(header);

    m_coordClient = std::make_shared<HcclCoordinatorClient>(m_commSize, m_rank, internal_unique_id);

    // First Handshake
    rc = firstHandShakeAtInit(header, hcclRankInfoHeaders);
    if (rc != hcclSuccess) return rc;

    LOG_HCL_INFO(HCL_COORD, "Rank Communicator handshake1 done");

    // Warning: This deviceId is hardcoded to 0 for the current single device
    // per process case. We need to change this later when one process supports
    // multi devices.
    const uint32_t device_id = 0;
    VERIFY(synEventCreate(&m_pdmaEventHandle, device_id, 0) == synSuccess, "synEventCreate failed");

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
    HclConfig config(hccl_device()->m_deviceConfig);
    if (!config.init(rank, commSize))
    {
        LOG_HCL_ERR(HCL, "Failed to initialize config with rank and commSize.");
        return hcclInternalError;
    }

    // create dynamic comm
    HCL_Comm hclCommId = hccl_device()->allocateNewComm();
    m_comm = &hccl_device()->getComm(hclCommId);
    m_comm->setUniqueID(internal_unique_id);

    // handle loopback mode and null submission
    bool isLoopbackModeOrNullSubmission =
        (IS_DEVICE_GEN2ARCH(hccl_device()->getDeviceType()) && (isLoopbackMode() || GCFG_HCL_NULL_SUBMIT.value()));

    int boxSize = m_boxSize;
    commSize    = m_commSize;
    rank        = m_rank;
    if (isLoopbackModeOrNullSubmission)
    {
        // workaround: in loopback mode we start with comm size = 1, but need to resize to 8
        m_comm->m_commSize = config.m_commSize;
        commSize           = config.m_commSize;
        boxSize            = hccl_device()->getHal()->getDefaultBoxSize();
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

    initializeRanks(hcclRankInfoHeaders, commSize, isLoopbackModeOrNullSubmission);

    std::vector<RemoteDeviceConnectionInfo> hcclRemoteDevices(m_commSize);

    LOG_HCL_INFO(HCL_COORD, "Rank initializeConnections start");
    rc = initializeConnections(isLoopbackModeOrNullSubmission);
    if (rc != hcclSuccess) return rc;

    // Second Handshake
    LOG_HCL_INFO(HCL_COORD, "Rank Communicator handshake2 start");
    rc = secondHandShakeAtInit(hcclRemoteDevices, isLoopbackModeOrNullSubmission);
    if (rc != hcclSuccess) return rc;
    LOG_HCL_INFO(HCL_COORD, "Rank Communicator handshake2 done");

    rc = finalizeInitialization(hcclRemoteDevices, isLoopbackModeOrNullSubmission);
    if (rc != hcclSuccess) return rc;
    LOG_HCL_INFO(HCL_COORD, "Rank Communicator init done");

    return rc;
}

hcclResult_t hccl_communicator::sendCollectiveLogErr()
{
    return m_coordClient->sendCollectiveLogErr();
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

    m_coordClient->destroy();

    synStatus status = synEventDestroy(m_pdmaEventHandle);
    if (status != synSuccess)
    {
        LOG_HCL_ERR(HCL, "Failed to destroy PDMA stream event");
        return false;
    }

    return true;
}

void hccl_communicator::finalize()
{
    if (GCFG_HCL_NULL_SUBMIT.value()) return;

    for (size_t i = 0; i < m_comm->m_streamLatestLongSo.size(); i++)
    {
        LOG_HCL_DEBUG(HCL, "Wait on streamId: {}, targetVal: {}", i, m_comm->m_streamLatestLongSo[i]);
        hccl_device()->getScalManager().synchronizeStream(i, m_comm->m_streamLatestLongSo[i]);
    }
    LOG_HCL_DEBUG(HCL, "Finalized");
}

hccl_communicator::hccl_communicator(int rank, int comm_size)
: m_rank(rank), m_commSize(comm_size)
{
}

void hccl_communicator::incCollectiveCtr()
{
    m_comm->incCollectiveCtr();
}

const uint64_t hccl_communicator::getCollectiveCtr()
{
    return m_comm->getCollectiveCtr();
}

const uint64_t hccl_communicator::incSendCtr(int peer)
{
    return m_comm->incSendCtr(peer);
}

const uint64_t hccl_communicator::getSendCtr(int peer)
{
    return m_comm->getSendCtr(peer);
}

const uint64_t hccl_communicator::incRecvCtr(int peer)
{
    return m_comm->incRecvCtr(peer);
}

const uint64_t hccl_communicator::getRecvCtr(int peer)
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

hcclResult_t hccl_communicator::syn_device(int* device)
{
    RETURN_ON_NULL_ARG(device);
    *device = static_cast<int>(hccl_device()->m_deviceId);
    LOG_HCL_DEBUG(HCL, "Communicator Device ID is: {}", (int)*device);
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
    // TODO: When it is not a success?
    *async_error = hcclSuccess;
    return hcclSuccess;
}

bool hccl_communicator::syncBetweenRanks()
{
    return m_coordClient->syncBetweenRanks();
}
