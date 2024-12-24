#include "api_aggregator.h"
#include <unordered_set>                                        // for unordered_set
#include "hcl_exceptions.h"                                     // for NotImplementedExc...
#include "hcl_utils.h"                                          // for LOG_HCL_INFO, VERIFY
#include "interfaces/hcl_icollective_routines.h"                // for IHclCollectiveRou...
#include "platform/gen2_arch_common/group_calls.h"              // for GroupCalls
#include "hcl_log_manager.h"                                    // for LOG_*
#include "platform/gen2_arch_common/hcl_collective_routines.h"  // for HclCollectiveRoutinesGen2Arch
#include "platform/gen2_arch_common/collective_states.h"
#include "platform/gen2_arch_common/hcl_device.h"
#include "hccl_context.h"
#include "collective_interface/graph_collectives.h"

ApiAggregatorGen2Arch::ApiAggregatorGen2Arch(HclCollectiveRoutinesGen2Arch* collectiveRoutines)
: m_collectiveRoutines(collectiveRoutines)
{
}

hcclResult_t ApiAggregatorGen2Arch::addGroupStart()
{
    m_collectiveRoutines->setGroupContext(true);
    ++m_counter;
    return hcclSuccess;
}

hcclResult_t ApiAggregatorGen2Arch::onGroupEnd()
{
    if (m_groupCalls.size() == 0 && m_sendRecvMemCpyVec.size() == 0)
    {
        m_collectiveRoutines->setGroupContext(false);
        return hcclSuccess;
    }

    for (auto comm : m_comms)
    {
        m_collectiveRoutines->sendRecv(m_groupCalls[comm],
                                       m_sendRecvMemCpyVec,
                                       comm,
                                       m_remoteRanks,
                                       hccl_ctx.generateApiId());
    }

    m_groupCalls.clear();
    m_sendRecvMemCpyVec.clear();
    m_collectiveRoutines->setGroupContext(false);

    return hcclSuccess;
}

uint64_t ApiAggregatorGen2Arch::checkGroupCollectiveDependency()
{
    uint64_t tempTargetVal, retTargetVal = 0;
    uint64_t nextTargetVal = m_collectiveRoutines->getCurrentTargetValue() + 1;

    // handle all send-recv calls first
    for (SendRecvApiEntry& entry : m_sendRecvStack)
    {
        tempTargetVal = m_collectiveRoutines->checkSendRecvDependency(entry.address,
                                                                      entry.count * dataTypeSizeInBytes(entry.dataType),
                                                                      nextTargetVal,
                                                                      entry.apiType == ApiType::Send,
                                                                      false);
        retTargetVal  = std::max(retTargetVal, tempTargetVal);
    }

    for (SendRecvApiEntry& sendEntry : m_selfSendRecvStack[ApiType::Send])
    {
        tempTargetVal =
            m_collectiveRoutines->checkSendRecvDependency(sendEntry.address,
                                                          sendEntry.count * dataTypeSizeInBytes(sendEntry.dataType),
                                                          nextTargetVal,
                                                          true,
                                                          false);
        retTargetVal = std::max(retTargetVal, tempTargetVal);
    }

    for (SendRecvApiEntry& recvEntry : m_selfSendRecvStack[ApiType::Recv])
    {
        tempTargetVal =
            m_collectiveRoutines->checkSendRecvDependency(recvEntry.address,
                                                          recvEntry.count * dataTypeSizeInBytes(recvEntry.dataType),
                                                          nextTargetVal,
                                                          false,
                                                          false);
        retTargetVal = std::max(retTargetVal, tempTargetVal);
    }

    // handle all collective calls next
    for (HclCollectiveParams& params : m_collectiveStack)
    {
        auto&&      device = m_collectiveRoutines->getDevice();
        CommonState commonState {
            params,
            m_collectiveRoutines->getIntermediateBufferManager(),
            false,
            device->getScaleOutProvider()->isGaudiDirect(),
            device->getEdmaEngineWorkDistributionSize(),
            device->getServerConnectivity().getMaxNumScaleUpPortsPerConnection(params.m_dynamicComm),
            params.m_dynamicComm.getCommConnectivity().getNumScaleOutPorts(),
            device->getSignalsCalculator(),
            m_collectiveRoutines->m_remainderCalculator};
        tempTargetVal = m_collectiveRoutines->checkCollectiveDependency(commonState, nextTargetVal, false);
        retTargetVal  = std::max(retTargetVal, tempTargetVal);
    }

    m_collectiveRoutines->setGroupMaxTargetValue(retTargetVal);
    return retTargetVal;
}

SendRecvApiEntries ApiAggregatorGen2Arch::createScaleoutExpandedVector(const SendRecvApiEntry& entry) const
{
    SendRecvApiEntries result;
    const uint64_t     sliceSize        = hccl_device()->getComm(entry.comm).getSliceSize();
    const HCL_Rank     remoteRank       = entry.remoteRank;
    const uint64_t     maxCountPerSlice = sizeToCount(sliceSize, entry.dataType);
    const uint64_t     iterCount        = entry.count;
    LOG_HCL_TRACE(HCL, "iterCount={}, remoteRank={}, maxCountPerSlice={}", iterCount, remoteRank, maxCountPerSlice);
    const uint64_t slicedEntries = (iterCount - 1) / maxCountPerSlice + 1;
    LOG_HCL_TRACE(HCL,
                  "Spliting large entry, iterCount={}, slicedEntries={}, remoteRank={}",
                  iterCount,
                  slicedEntries,
                  remoteRank);
    VERIFY(slicedEntries > 1, "Invalid number of split entries");
    uint64_t remainCount   = iterCount;
    uint64_t slicesCounter = 0;
    uint64_t currAddr      = entry.address;
    while (slicesCounter < slicedEntries)
    {
        const uint64_t currCount =
            (slicesCounter < (slicedEntries - 1)) ? maxCountPerSlice : remainCount;  // send each time up to sliceSize
        VERIFY(currCount > 0, "Invalid split entry size");

        SendRecvApiEntry partialEntry(entry);  // copy all fields of original entry and update the segment
        partialEntry.address = currAddr;
        partialEntry.count   = currCount;

        LOG_HCL_TRACE(HCL,
                      "Adding entry, slicesCounter={}, count={}, address=0x{:x}, remoteRank={}",
                      slicesCounter,
                      partialEntry.count,
                      partialEntry.address,
                      partialEntry.remoteRank);
        result.push_back(partialEntry);
        currAddr += sliceSize;
        slicesCounter++;
        remainCount -= currCount;
    };

    return result;
}

void ApiAggregatorGen2Arch::onHandleSendRecvEntry(SendRecvApiEntry& sendRecvEntry)
{
    hcl::SchedulersIndex index;

    switch (sendRecvEntry.apiType)
    {
        case ApiType::Send:
        {
            index = sendRecvEntry.isRankInsideScaleupGroup ? hcl::SchedulersIndex::sendScaleUp
                                                           : hcl::SchedulersIndex::sendScaleOut;

            break;
        }
        case ApiType::Recv:
        {
            index = sendRecvEntry.isRankInsideScaleupGroup ? hcl::SchedulersIndex::recvScaleUp
                                                           : hcl::SchedulersIndex::recvScaleOut;
            break;
        }
        default:
            throw hcl::NotImplementedException("API not implemented for grouping");
            break;
    };

    // Check if we need to slice send/recv entries to GCFG_HCL_SLICE_SIZE chunks
    // Note that for gnics, the actual size can be bigger ( arc_cmd_send_recv_short_t::cache_line_count field size (22
    // bits) * ARC_CMD_SEND_RECV_SHORT_SIZE (64)  = 256MB elements per active nic), but for simplicity
    // we do same slicing for hnics and gnics, for both scaleup and scaleout.
    const uint64_t maxCountPerSlice =
        sizeToCount(hccl_device()->getComm(sendRecvEntry.comm).getSliceSize(), sendRecvEntry.dataType);
    if (sendRecvEntry.count > maxCountPerSlice)
    {
        LOG_HCL_TRACE(HCL,
                      "Splitting large entry, count={}, maxCountPerIMB={}, remoteRank={}",
                      sendRecvEntry.count,
                      maxCountPerSlice,
                      sendRecvEntry.remoteRank);
        const SendRecvApiEntries expandedEntries(createScaleoutExpandedVector(sendRecvEntry));
        VERIFY(expandedEntries.size() >= 2, "Entries split not correct");
        for (const SendRecvApiEntry& partialEntry : expandedEntries)
        {
            LOG_HCL_TRACE(HCL,
                          "Adding entry, count={}, address=0x{:x}, remoteRank={}",
                          partialEntry.count,
                          partialEntry.address,
                          partialEntry.remoteRank);
            m_groupCalls[partialEntry.comm][index].addCall(partialEntry);
        }
    }
    else
    {
        LOG_HCL_TRACE(HCL,
                      "Adding non-split entry, count={}, address=0x{:x}, remoteRank={}",
                      sendRecvEntry.count,
                      sendRecvEntry.address,
                      sendRecvEntry.remoteRank);
        m_groupCalls[sendRecvEntry.comm][index].addCall(sendRecvEntry);
    }
}

bool ApiAggregatorGen2Arch::checkCallsCounter()
{
    if (m_calls++ >= MAX_AGG_OPS)
    {
        LOG_HCL_ERR(HCL, "max ops reached for group call");
        return false;
    }

    return true;
}

hcclResult_t ApiAggregatorGen2Arch::addSendRecvApiCall(HCL_Rank myRank, const SendRecvApiEntry& entry)
{
    if (!checkCallsCounter()) return hcclInvalidUsage;

    addGroupStart();

    m_comms.insert(entry.comm);

    if (myRank == entry.remoteRank)
    {
        LOG_HCL_TRACE(HCL, "addSendRecvApiCall to m_selfSendRecvStack");
        m_selfSendRecvStack[entry.apiType].push_back(entry);
    }
    else
    {
        LOG_HCL_TRACE(HCL, "addSendRecvApiCall to m_sendRecvStack");
        m_sendRecvStack.push_back(entry);
        m_remoteRanks.insert(entry.remoteRank);
    }

    return addGroupEnd();
}

hcclResult_t ApiAggregatorGen2Arch::addCollectiveApiCall(HclCollectiveParams& params)
{
    if (m_counter == 0)  // no group mode
    {
        if (CHECK_PRIM_IMPL(params.m_collectiveOp))
        {
            return run(m_collectiveRoutines, params);
        }
        else
        {
            return m_collectiveRoutines->hclCollectiveCall(params);
        }
    }

    if (!checkCallsCounter()) return hcclInvalidUsage;

    m_comms.insert(params.m_dynamicComm);
    m_collectiveStack.push_back(params);

    return hcclSuccess;
}

hcclResult_t ApiAggregatorGen2Arch::addGroupEnd()
{
    --m_counter;

    if (m_counter > 0)
    {
        LOG_HCL_INFO(HCL, "nested groupEnd, group call not executed: {}", m_counter);
        return hcclSuccess;
    }

    if (m_counter < 0)
    {
        LOG_HCL_ERR(HCL, "Odd number of groupStart & groupEnd calls or too many groupEnd, m_counter={}", m_counter);
        return hcclInvalidUsage;
    }

    if (m_comms.size() == 0) return hcclSuccess;
    ;

    checkGroupCollectiveDependency();

    // handle all send-recv calls first
    while (m_sendRecvStack.size())
    {
        SendRecvApiEntry& entry = m_sendRecvStack.front();
        onHandleSendRecvEntry(entry);
        m_sendRecvStack.pop_front();
    }
    handleSelfSendRecv();

    // handle all collective calls next
    while (m_collectiveStack.size())
    {
        HclCollectiveParams& params = m_collectiveStack.front();
        m_collectiveRoutines->hclCollectiveCall(params);
        m_collectiveStack.pop_front();
    }

    onGroupEnd();

    m_calls = 0;

    m_comms.clear();
    m_remoteRanks.clear();

    return hcclSuccess;
}

void ApiAggregatorGen2Arch::handleSelfSendRecv()
{
    VERIFY(m_selfSendRecvStack[ApiType::Send].size() == m_selfSendRecvStack[ApiType::Recv].size(),
           "Self send and self receive should have the same number of calls, got {} self send and {} self receive",
           m_selfSendRecvStack[ApiType::Send].size(),
           m_selfSendRecvStack[ApiType::Recv].size());

    while (m_selfSendRecvStack[ApiType::Send].size())
    {
        SendRecvApiEntry sendEntry = m_selfSendRecvStack[ApiType::Send].front();
        m_selfSendRecvStack[ApiType::Send].pop_front();

        SendRecvApiEntry recvEntry = m_selfSendRecvStack[ApiType::Recv].front();
        m_selfSendRecvStack[ApiType::Recv].pop_front();

        VERIFY(recvEntry.count == sendEntry.count && recvEntry.dataType == sendEntry.dataType &&
                   recvEntry.streamHandle == sendEntry.streamHandle,
               "Mismatch size of self send and self receive calls, send size is {} and receive size is {}",
               sendEntry.count,
               recvEntry.count);

        if (recvEntry.address == sendEntry.address)
        {
            LOG_HCL_DEBUG(HCL, "Skipping self send-receive commands to address {}", recvEntry.address);
            continue;
        }

        m_sendRecvMemCpyVec.push_back({recvEntry.count, recvEntry.dataType, recvEntry.address, sendEntry.address});
    }
}
