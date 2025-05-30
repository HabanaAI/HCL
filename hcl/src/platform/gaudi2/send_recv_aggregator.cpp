#include "platform/gaudi2/send_recv_aggregator.h"

#include <cstdint>  // for uint32_t

#include "hcl_utils.h"                                       // for LOG_HCL_TRACE
#include "hcl_log_manager.h"                                 // for LOG_*
#include "platform/gaudi2/commands/hcl_commands.h"           // for HclCommandsGaudi2
#include "platform/gaudi2/context_manager.h"                 // for ContextManager
#include "platform/gaudi2/hcl_count_descriptor.h"            // for CountDescriptor
#include "platform/gen2_arch_common/send_recv_aggregator.h"  // for SendRecvEntry
#include "platform/gen2_arch_common/server_connectivity.h"   // for Gen2ArchServerConnectivity
#include "hcl_types.h"

class HclCommandsGen2Arch;
namespace hcl
{
class ScalStreamBase;
}

SendRecvAggregator::SendRecvAggregator(const std::vector<unsigned>&      nicEngines,
                                       const Gen2ArchServerConnectivity& serverConnectivity,
                                       HclCommandsGen2Arch&              commands)
: SendRecvAggregatorBase(),
  m_commands((HclCommandsGaudi2&)commands),
  m_serverConnectivity(serverConnectivity),
  m_nicPassthroughHandler(nicEngines, serverConnectivity, commands)
{
}

bool SendRecvAggregator::getRequiredContext(RequiredCollectiveContext& requiredContext)
{
    if (!m_requiredContextSet) return false;

    requiredContext = m_requiredContext;
    return true;
}

void SendRecvAggregator::addSendRecvArray(const SendRecvArray&              arr,
                                          int                               selfModuleId,
                                          [[maybe_unused]] unsigned         collectiveContextIndex,
                                          const RequiredCollectiveContext&& requiredContext)
{
    AggregatedEntryArray aggregatedArray {};
    for (unsigned deviceId = 0; deviceId < HLS2_BOX_SIZE; deviceId++)
    {
        if (deviceId == (unsigned)selfModuleId) continue;

        const SendRecvEntry& entry = arr[deviceId];
        aggregatedArray[deviceId]  = AggregatedEntry {entry, /*isLast=*/false};
    }
    m_aggEntryArrays.push_back(aggregatedArray);

    if (!m_requiredContextSet)
    {
        // There is a hidden assumption here that the collective context index used here is the same as in the last
        // call in the group, and thus the collective context is the same. Need to add a 'VERIFY' here.
        m_requiredContext    = requiredContext;
        m_requiredContextSet = true;
    }

    std::array<UniqueCollectiveContext, HLS2_BOX_SIZE> uniqueContexts =
        ContextManager::createUniqueContexts(m_requiredContext);
    for (unsigned deviceIndex = 0; deviceIndex < HLS2_BOX_SIZE; deviceIndex++)
    {
        if (!uniqueContexts[deviceIndex].connection_enabled && arr[deviceIndex].isValid)
        {
            UniqueCollectiveContext uniqueContext;
            uniqueContext.connection_enabled = arr[deviceIndex].isValid;
            uniqueContext.remote_index       = arr[deviceIndex].isValid ? 0 : -1;
            uniqueContexts[deviceIndex]      = uniqueContext;
        }
    }
    m_requiredContext.m_remoteDescriptor = ContextManager::createRemoteDescriptor(uniqueContexts);
}

void SendRecvAggregator::configureLastEntriesPerDevice()
{
    AggregatedEntryArray& aggregatedEntryVector = *m_aggEntryArrays.rbegin();
    for (unsigned deviceId = 0; deviceId < HLS2_BOX_SIZE; deviceId++)
    {
        if (aggregatedEntryVector[deviceId].data.isValid)
        {
            aggregatedEntryVector[deviceId].isLast = true;
            continue;
        }
    }
}

void SendRecvAggregator::flush(hcl::ScalStreamBase&             scalStream,
                               [[maybe_unused]] ContextManager& contextManager,
                               unsigned                         collectiveContextIndex,
                               unsigned                         commDescIndex,
                               int                              selfModuleId,
                               HCL_Comm                         comm,
                               unsigned                         syncObjectAddressIndex,
                               bool                             isSend,
                               bool                             notifyRndvAck,
                               bool                             waitForRndvAcks)
{
    LOG_HCL_TRACE(HCL, "Flush for send/recv aggregator triggered for {} arrays", m_aggEntryArrays.size());
    configureLastEntriesPerDevice();
    for (unsigned i = 0; i < m_aggEntryArrays.size(); i++)
    {
        AggregatedEntryArray& aggregatedEntryVector = m_aggEntryArrays[i];

        DwordsBoxesArray buffer;

        for (size_t deviceId = 0; deviceId < m_serverConnectivity.getNumberOfDevicesPerHost(); deviceId++)
        {
            const AggregatedEntry& entry = aggregatedEntryVector[deviceId];

            if (deviceId == (unsigned)selfModuleId) continue;
            else if (entry.data.isValid)
            {
                LOG_HCL_TRACE(HCL,
                              "deviceId {}: detected a valid user send/recv, address=0x{:x}, count={}, remoteRank={}",
                              deviceId,
                              entry.data.address,
                              entry.data.count,
                              entry.data.remoteRank);
                CountDescriptor countDesc(entry.data.count,
                                          contextManager.getServerConnectivity().getMaxNumScaleUpPortsPerConnection());
                m_commands.serializeUserSendCommand(buffer[deviceId],
                                                    collectiveContextIndex,
                                                    commDescIndex,
                                                    syncObjectAddressIndex,
                                                    countDesc.m_cacheLineCount,
                                                    countDesc.m_cacheLineRemainder,
                                                    countDesc.m_elementRemainder,
                                                    entry.data.dataType,
                                                    entry.data.address,
                                                    entry.isLast,
                                                    notifyRndvAck,
                                                    waitForRndvAcks);
            }
        }

        m_nicPassthroughHandler.addDeviceBuffer(buffer, comm);  // adds new items to records ("new")
    }

    m_nicPassthroughHandler
        .flush(scalStream, collectiveContextIndex, selfModuleId, comm, syncObjectAddressIndex, isSend);

    m_aggEntryArrays.clear();
    m_requiredContextSet = false;
}
