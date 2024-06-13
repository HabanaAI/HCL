#include "ofi_communicator.h"
#include <array>                                  // for array, array<>::val...
#include <cstdint>                                // for uint64_t
#include <cstring>                                // for memcpy
#include <utility>                                // for pair
#include "hccl_internal_defs.h"                   // for hcclHandle, hcclHan...
#include "hcl_types.h"                            // for RankInfo, HostNicInfo
#include "hcl_utils.h"                            // for LOG_HCL_ERR, LOG_HC...
#include "interfaces/hcl_unique_sorted_vector.h"  // for UniqueSortedVector
#include "libfabric/libfabric_common.h"           // for post_recv, post_send
#include "libfabric/mr_mapping.h"                 // for MRMapping
#include "libfabric/hl_ofi.h"                     // for ofi_t, OFI_UNLIKELY
#include "libfabric/hl_ofi_component.h"           // for allConnectionComm
#include "hcl_log_manager.h"                      // for LOG_ERR, LOG_DEBUG, LOG_INFO
#include "infra/hcl_debug_stats.h"                // for DEBUG_STATS_...

ofi_communicator::ofi_communicator() : my_rank_(-1) {}

bool ofi_communicator::initializeCommunicator(int                       hcclRank,
                                              int                       nranks,
                                              const UniqueSortedVector& peers,
                                              IHclDevice*               hclDevice,
                                              RankInfo&                 rankInfo)
{
    LOG_HCL_TRACE(HCL, "hcclRank={}, nranks={}, peers=[ {} ]", hcclRank, nranks, peers);
    m_myRankInfo = &rankInfo;
    int status;
    if (peers.size() == 0)
    {
        LOG_HCL_DEBUG(HCL, "Rank {} has no peers, skipping OFI communicator initialization", hcclRank);
        return true;
    }

    if (m_peerRankToConnectionInfo.empty())
    {
        m_peerRankToConnectionInfo.resize(nranks);
    }

    m_ofi_        = hclDevice->getOfiHandle();
    m_ofiDeviceId = hclDevice->getOfiDeviceId();
    m_device_     = hclDevice;
    my_rank_      = hcclRank;

    for (const HCL_Rank peer : peers)
    {
        for (unsigned hostConnIdx = 0; hostConnIdx < getNumConnectionPerRank(); hostConnIdx++)
        {
            char buff[CTRL_BUF_SIZE] = {0};
            status = m_ofi_->listen(m_ofiDeviceId, &buff, &m_peerRankToConnectionInfo[peer][hostConnIdx].listenComm);

            if (status)
            {
                LOG_HCL_ERR(HCL, "listen returned failure from rank {} to rank {}", my_rank_, peer);
                return false;
            }
            std::memcpy(rankInfo.remoteInfo[peer].hostNicConns.server[hostConnIdx].buff, buff, CTRL_BUF_SIZE);
        }
    }

    threads_manager_.initThreads(0, my_rank_);

    return true;
}

bool ofi_communicator::updateConnections(const HCL_Rank outerRank, const HostNicConnectInfo& hnicsInfoBuf)
{
    for (unsigned hostConnIdx = 0; hostConnIdx < getNumConnectionPerRank(); hostConnIdx++)
    {
        // TODO: we convert const ptr to non-const because connect() requires it although it does not modify it.
        // Once connect() signature is fixed we remove the const_cast().

        HostNicConnOpaque& nonConstHnicsInfo = const_cast<HostNicConnOpaque&>(hnicsInfoBuf.server[hostConnIdx]);
        int                status;

        if (my_rank_ < outerRank)
        {
            status = m_ofi_->connect(m_ofiDeviceId,
                                     (void*)&(nonConstHnicsInfo.buff),
                                     &m_peerRankToConnectionInfo[outerRank][hostConnIdx].sendComm,
                                     m_myRankInfo->remoteInfo[outerRank].hostNicConns.server[hostConnIdx].buff);
            if (status)
            {
                LOG_HCL_ERR(HCL, "connect returned failure from rank {} to rank {}", my_rank_, outerRank);
                return false;
            }
        }
        else
        {
            status = m_ofi_->accept(m_peerRankToConnectionInfo[outerRank][hostConnIdx].listenComm,
                                    &m_peerRankToConnectionInfo[outerRank][hostConnIdx].recvComm);
            if (status)
            {
                LOG_HCL_ERR(HCL, "accept returned failure from rank {} to rank {}", my_rank_, outerRank);
                return false;
            }
        }

        if (my_rank_ > outerRank)
        {
            status = m_ofi_->connect(m_ofiDeviceId,
                                     (void*)&(nonConstHnicsInfo.buff),
                                     &m_peerRankToConnectionInfo[outerRank][hostConnIdx].sendComm,
                                     m_myRankInfo->remoteInfo[outerRank].hostNicConns.server[hostConnIdx].buff);
            if (status)
            {
                LOG_HCL_ERR(HCL, "connect returned failure from rank {} to rank {}", my_rank_, outerRank);
                return false;
            }
        }
        else
        {
            status = m_ofi_->accept(m_peerRankToConnectionInfo[outerRank][hostConnIdx].listenComm,
                                    &m_peerRankToConnectionInfo[outerRank][hostConnIdx].recvComm);
            if (status)
            {
                LOG_HCL_ERR(HCL, "accept returned failure from rank {} to rank {}", my_rank_, outerRank);
                return false;
            }
        }
    }

    return true;
}

hcclResult_t ofi_communicator::sendAsync(void*                  sendbuff,
                                         size_t                 size,
                                         int                    peer,
                                         hcclHandle*            handle,
                                         unsigned               hostConnIdx,
                                         OfiCompCallbackParams& compParams)
{
    HCL_FUNC_INSTRUMENTATION(DEBUG_STATS_ALL);

    if (!m_ofi_->is_initialized())
    {
        LOG_HCL_ERR(HCL, "ofi must be initialized before any send");
        return hcclLibfabricError;
    }

    int status = post_send(m_peerRankToConnectionInfo[peer][hostConnIdx].sendComm,
                           sendbuff,
                           size,
                           &handle->ofi.req,
                           m_ofi_,
                           compParams);
    if (status)
    {
        LOG_HCL_ERR(HCL, "send from {} to {} failed", my_rank_, peer);
        return hcclLibfabricError;
    }

    handle->isOfiReq       = true;
    handle->ofi.recvBuffer = nullptr;
    handle->ofi.size       = size;

    return hcclSuccess;
}

hcclResult_t ofi_communicator::recvAsync(void*                  recvbuff,
                                         size_t                 size,
                                         int                    peer,
                                         hcclHandle*            handle,
                                         unsigned               hostConnIdx,
                                         OfiCompCallbackParams& compParams)
{
    HCL_FUNC_INSTRUMENTATION(DEBUG_STATS_ALL);

    if (!m_ofi_->is_initialized())
    {
        LOG_HCL_ERR(HCL, "ofi must be initialized before any recv");
        return hcclLibfabricError;
    }

    int status = post_recv(m_peerRankToConnectionInfo[peer][hostConnIdx].recvComm,
                           recvbuff,
                           size,
                           &handle->ofi.req,
                           m_ofi_,
                           compParams);
    if (status)
    {
        LOG_HCL_ERR(HCL, "receive from {} to {} failed", peer, my_rank_);
        return hcclLibfabricError;
    }

    handle->isOfiReq       = true;
    handle->ofi.ofiComm    = m_peerRankToConnectionInfo[peer][hostConnIdx].recvComm;
    handle->ofi.recvBuffer = recvbuff;
    handle->ofi.size       = size;

    return hcclSuccess;
}

bool ofi_communicator::waitForCompletionNb(void* handle, int& done)
{
    hcclOfiHandle* ofiHandle = (hcclOfiHandle*)handle;
    ofi_req_t*     request   = ofiHandle->req;

    int    status;
    size_t ssize = 0;

    status = m_ofi_->test(request, &done, &ssize);
    if (status)
    {
        done = 1;
        LOG_HCL_ERR(HCL, "test failed");
        return false;
    }

    return true;
}

bool ofi_communicator::destroy()
{
    for (const auto& peerRankConnections : m_peerRankToConnectionInfo)
    {
        for (const auto& hnicConn : peerRankConnections)
        {
            if (hnicConn.listenComm)
            {
                m_ofi_->close(hnicConn.listenComm);
            }
            if (hnicConn.sendComm)
            {
                m_ofi_->close(hnicConn.sendComm);
            }
            if (hnicConn.recvComm)
            {
                m_ofi_->close(hnicConn.recvComm);
            }
        }
    }
    threads_manager_.destroy();

    return true;
}

unsigned ofi_communicator::getNumConnectionPerRank()
{
    return (GCFG_ENABLE_HNIC_MICRO_STREAMS.value() ? MAX_HNIC_CONNECTIONS : 1);
}
