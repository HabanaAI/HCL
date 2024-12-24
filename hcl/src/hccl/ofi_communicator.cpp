#include "ofi_communicator.h"
#include <array>                                  // for array, array<>::val...
#include <cstdint>                                // for uint64_t
#include <cstring>                                // for memcpy
#include <utility>                                // for pair
#include "hccl_internal_defs.h"                   // for hcclHandle, hcclHan...
#include "hcl_types.h"                            // for RankInfo, HostNicInfo
#include "hcl_utils.h"                            // for LOG_HCL_ERR, LOG_HC...
#include "hcl_dynamic_communicator.h"             // for HclDynamicCommunicator
#include "interfaces/hcl_unique_sorted_vector.h"  // for UniqueSortedVector
#include "libfabric/libfabric_common.h"           // for ofiCommOp
#include "libfabric/hl_ofi.h"                     // for ofi_t, OFI_UNLIKELY
#include "libfabric/hl_ofi_component.h"           // for allConnectionComm
#include "hcl_log_manager.h"                      // for LOG_ERR, LOG_DEBUG, LOG_INFO
#include "infra/hcl_debug_stats.h"                // for DEBUG_STATS_...

ofi_communicator::ofi_communicator(std::shared_ptr<MemoryRegion> mr) : my_rank_(-1), m_mr(mr) {}

bool ofi_communicator::initializeCommunicator(int                       hcclRank,
                                              int                       nranks,
                                              const UniqueSortedVector& peers,
                                              IHclDevice*               hclDevice,
                                              RankInfo&                 rankInfo,
                                              const uint16_t            qpSetCount)
{
    LOG_HCL_TRACE(HCL, "hcclRank={}, nranks={}, peers=[ {} ], qpSetCount={}", hcclRank, nranks, peers, qpSetCount);
    m_myRankInfo = &rankInfo;
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
    // In case of multi qp per set an additional single-qp set is create for small sizes under threshold.
    m_qpSetCount = qpSetCount + (GCFG_HCL_SINGLE_QP_PER_SET.value() ? 0 : 1);
    my_rank_     = hcclRank;

    for (const HCL_Rank peer : peers)
    {
        for (uint16_t qpSetIndex = 0; qpSetIndex < m_qpSetCount; ++qpSetIndex)
        {
            for (unsigned hostConnIdx = 0; hostConnIdx < getNumConnectionPerRank(); hostConnIdx++)
            {
                char buff[CTRL_BUF_SIZE] = {0};
                int  status              = m_ofi_->listen(m_ofiDeviceId,
                                            &buff,
                                            &m_peerRankToConnectionInfo[peer][qpSetIndex][hostConnIdx].listenComm,
                                            hostConnIdx,
                                            qpSetIndex);
                if (status)
                {
                    LOG_HCL_ERR(HCL, "listen returned failure from rank {} to rank {}", my_rank_, peer);
                    return false;
                }
                std::memcpy(rankInfo.remoteInfo[peer].hostNicConns.server[qpSetIndex][hostConnIdx].buff,
                            buff,
                            CTRL_BUF_SIZE);
            }
        }
    }

    return true;
}

bool ofi_communicator::updateConnections(const HCL_Rank outerRank, const HostNicConnectInfo& hnicsInfoBuf)
{
    for (uint16_t qpSetIndex = 0; qpSetIndex < m_qpSetCount; ++qpSetIndex)
    {
        for (unsigned hostConnIdx = 0; hostConnIdx < getNumConnectionPerRank(); hostConnIdx++)
        {
            const HostNicConnOpaque& nonConstHnicsInfo = hnicsInfoBuf.server[qpSetIndex][hostConnIdx];
            int                      status            = 0;

            if (my_rank_ < outerRank)
            {
                status = m_ofi_->connect(
                    m_ofiDeviceId,
                    &(nonConstHnicsInfo.buff),
                    &m_peerRankToConnectionInfo[outerRank][qpSetIndex][hostConnIdx].sendComm,
                    m_myRankInfo->remoteInfo[outerRank].hostNicConns.server[qpSetIndex][hostConnIdx].buff,
                    hostConnIdx,
                    qpSetIndex);
                if (status)
                {
                    LOG_HCL_ERR(HCL, "connect returned failure from rank {} to rank {}", my_rank_, outerRank);
                    return false;
                }
            }
            else
            {
                status = m_ofi_->accept(m_peerRankToConnectionInfo[outerRank][qpSetIndex][hostConnIdx].listenComm,
                                        &m_peerRankToConnectionInfo[outerRank][qpSetIndex][hostConnIdx].recvComm);
                if (status)
                {
                    LOG_HCL_ERR(HCL, "accept returned failure from rank {} to rank {}", my_rank_, outerRank);
                    return false;
                }
            }

            if (my_rank_ > outerRank)
            {
                status = m_ofi_->connect(
                    m_ofiDeviceId,
                    &(nonConstHnicsInfo.buff),
                    &m_peerRankToConnectionInfo[outerRank][qpSetIndex][hostConnIdx].sendComm,
                    m_myRankInfo->remoteInfo[outerRank].hostNicConns.server[qpSetIndex][hostConnIdx].buff,
                    hostConnIdx,
                    qpSetIndex);
                if (status)
                {
                    LOG_HCL_ERR(HCL, "connect returned failure from rank {} to rank {}", my_rank_, outerRank);
                    return false;
                }
            }
            else
            {
                status = m_ofi_->accept(m_peerRankToConnectionInfo[outerRank][qpSetIndex][hostConnIdx].listenComm,
                                        &m_peerRankToConnectionInfo[outerRank][qpSetIndex][hostConnIdx].recvComm);
                if (status)
                {
                    LOG_HCL_ERR(HCL, "accept returned failure from rank {} to rank {}", my_rank_, outerRank);
                    return false;
                }
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
                                         OfiCompCallbackParams& compParams,
                                         uint16_t               qpSetIndex)
{
    HCL_FUNC_INSTRUMENTATION(DEBUG_STATS_ALL);

    if (!m_ofi_->is_initialized())
    {
        LOG_HCL_ERR(HCL, "ofi must be initialized before any send");
        return hcclLibfabricError;
    }

    int status = ofiCommOp(CommOp::SEND,
                           m_peerRankToConnectionInfo[peer][qpSetIndex][hostConnIdx].sendComm,
                           sendbuff,
                           size,
                           m_mr ? m_mr->getMRHandle() : NULL,
                           &handle->ofi.req,
                           m_ofi_,
                           compParams);
    if (status)
    {
        LOG_HCL_ERR(HCL, "send from {} to {} failed", my_rank_, peer);
        return hcclLibfabricError;
    }

    handle->ofi.recvBuffer = nullptr;
    handle->ofi.size       = size;

    return hcclSuccess;
}

hcclResult_t ofi_communicator::recvAsync(void*                  recvbuff,
                                         size_t                 size,
                                         int                    peer,
                                         hcclHandle*            handle,
                                         unsigned               hostConnIdx,
                                         OfiCompCallbackParams& compParams,
                                         uint16_t               qpSetIndex)
{
    HCL_FUNC_INSTRUMENTATION(DEBUG_STATS_ALL);

    if (!m_ofi_->is_initialized())
    {
        LOG_HCL_ERR(HCL, "ofi must be initialized before any recv");
        return hcclLibfabricError;
    }

    int status = ofiCommOp(CommOp::RECV,
                           m_peerRankToConnectionInfo[peer][qpSetIndex][hostConnIdx].recvComm,
                           recvbuff,
                           size,
                           m_mr ? m_mr->getMRHandle() : NULL,
                           &handle->ofi.req,
                           m_ofi_,
                           compParams);
    if (status)
    {
        LOG_HCL_ERR(HCL, "receive from {} to {} failed", peer, my_rank_);
        return hcclLibfabricError;
    }

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
        for (uint32_t i = 0; i < m_qpSetCount; ++i)
        {
            const auto& qpSet = peerRankConnections[i];
            for (const auto& hnicConn : qpSet)
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
    }

    return true;
}

unsigned ofi_communicator::getNumConnectionPerRank()
{
    return (GCFG_ENABLE_HNIC_MICRO_STREAMS.value() ? MAX_HNIC_CONNECTIONS : 1);
}
