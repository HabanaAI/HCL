#include "platform/gaudi3/send_recv_aggregator.h"

#include <algorithm>      // for max
#include <cstdint>        // for uint32_t
#include <unordered_set>  // for unordered_set
#include <sstream>        // stringstream
#include <utility>        // for tuple_size
#include <set>            // for set

#include "hcl_api_types.h"                                    // for HCL_Comm
#include "hcl_utils.h"                                        // for LOG_HCL_TRACE
#include "hcl_log_manager.h"                                  // for LOG_*
#include "platform/gaudi3/commands/hcl_commands.h"            // for HclCommandsGaudi4
#include "platform/gen2_arch_common/send_recv_aggregator.h"   // for SendRecvEntry
#include "hcl_types.h"                                        // for HCL_HwModuleId
#include "platform/gaudi3/gaudi3_base_server_connectivity.h"  // for Gaudi3BaseServerConnectivity

namespace hcl
{
class ScalStreamBase;
}

SendRecvAggregatorGaudi3::SendRecvAggregatorGaudi3(const bool                          isSend,
                                                   const uint32_t                      selfModuleId,
                                                   const Gaudi3BaseServerConnectivity& serverConnectivity,
                                                   const DevicesSet&                   hwModules,
                                                   HclCommandsGaudi3&                  commands)
: SendRecvAggregatorBase(),
  m_isSend(isSend),
  m_selfModuleId(selfModuleId),
  m_serverConnectivity(serverConnectivity),
  m_hwModules(hwModules),
  m_commands(commands),
  m_nicPassthroughHandlerSet0(isSend, true /*isSet0*/, serverConnectivity, commands),
  m_nicPassthroughHandlerSet1(isSend, false /*isSet0*/, serverConnectivity, commands)
{
}

void SendRecvAggregatorGaudi3::addSendRecvArray(const SendRecvArray& arr)
{
    LOG_HCL_TRACE(HCL, "m_selfModuleId={}, m_aggEntryArrays.size={}", m_selfModuleId, m_aggEntryArrays.size());
    size_t index = 0;
    for (auto& entry : arr)
    {
        if (entry.isValid)
        {
            LOG_HCL_TRACE(HCL, "arr[{}]={}", index, entry);
        }
        index++;
    }
    AggregatedEntryArray aggregatedArray {};
    for (uint32_t deviceId = 0; deviceId < m_serverConnectivity.getNumberOfDevicesPerHost(); deviceId++)
    {
        if (deviceId == m_selfModuleId) continue;

        const SendRecvEntry& entry = arr[deviceId];
        aggregatedArray[deviceId]  = AggregatedEntry {entry, /*isNop*/ false, /*isLast=*/false};
    }
    m_aggEntryArrays.push_back(aggregatedArray);
    LOG_HCL_TRACE(HCL, "After adding, m_aggEntryArrays.size={}", m_aggEntryArrays.size());
}

void SendRecvAggregatorGaudi3::flush(hcl::ScalStreamBase& scalStream,
                                     const HCL_Comm       comm,
                                     const uint8_t        dcore,
                                     const uint8_t        ssm,
                                     const uint16_t       sobId,
                                     const uint32_t       qpn)
{
    LOG_HCL_TRACE(
        HCL,
        "Flush for send/recv aggregator triggered for comm={}, {} arrays, m_selfModuleId={}, m_isSend={}, qpn={}",
        comm,
        m_aggEntryArrays.size(),
        m_selfModuleId,
        m_isSend,
        qpn);

    uint16_t set0DupMask = 0;
    uint16_t set1DupMask = 0;

    DevicesSet devicesInSet0;
    DevicesSet devicesInSet1;

    uint32_t set0PortEnableMask = 0;
    uint32_t set1PortEnableMask = 0;

    LOG_HCL_TRACE(HCL, "m_hwModules=[ {} ]", m_hwModules);

    for (unsigned i = 0; i < m_aggEntryArrays.size(); i++)
    {
        const AggregatedEntryArray& arr = m_aggEntryArrays[i];

        for (const HCL_HwModuleId deviceId : m_hwModules)
        {
            if (deviceId == m_selfModuleId) continue;

            VERIFY(m_serverConnectivity.getDevicesSet(true, comm).count(deviceId) ||
                       m_serverConnectivity.getDevicesSet(false, comm).count(deviceId),
                   "Device {} not in any nic macro set!",
                   deviceId);

            // a device can only belong to first or second set, not both
            const bool             isSet0 = (m_serverConnectivity.getDevicesSet(true, comm).count(deviceId) == 1);
            const AggregatedEntry& entry  = arr[deviceId];
            if (entry.data.isValid)
            {
                LOG_HCL_TRACE(HCL,
                              "deviceId {}: In dup mask loop, detected a valid user send/recv, isSet0={}, "
                              "address=0x{:x}, count={}, remoteRank={}",
                              deviceId,
                              isSet0,
                              entry.data.address,
                              entry.data.count,
                              entry.data.remoteRank);
                if (isSet0)
                {
                    VERIFY(devicesInSet1.count(deviceId) == 0, "device {} is in set1!", deviceId);
                    devicesInSet0.insert(deviceId);
                    set0DupMask |= m_serverConnectivity.getNicsMacrosDupMask(deviceId, comm);
                    set0PortEnableMask |= m_serverConnectivity.getRemoteDevicesPortMasks(comm)[deviceId];
                }
                else
                {
                    VERIFY(devicesInSet0.count(deviceId) == 0, "device {} is in set0", deviceId);
                    devicesInSet1.insert(deviceId);
                    set1DupMask |= m_serverConnectivity.getNicsMacrosDupMask(deviceId, comm);
                    set1PortEnableMask |= m_serverConnectivity.getRemoteDevicesPortMasks(comm)[deviceId];
                }
            }
        }
    }

    // Check if we can merge tha devices from both sets
    bool canMergeSets = true;
    if ((set0DupMask & set1DupMask))  // collide - use 2 sets if it have enough devices
    {
        canMergeSets = false;
        LOG_HCL_DEBUG(HCL, "Cross nic macro pairs detected, need to split aggregation to 2 sets");
        if (unlikely(LOG_LEVEL_AT_LEAST_TRACE(HCL)))
        {
            for (const auto device : devicesInSet0)
            {
                LOG_HCL_TRACE(HCL, "devicesInSet0: device={}", device);
            }
            for (const auto device : devicesInSet1)
            {
                LOG_HCL_TRACE(HCL, "devicesInSet1: device={}", device);
            }
        }
    }

    if (canMergeSets)
    {
        devicesInSet0.merge(devicesInSet1);
        devicesInSet1.clear();
        set0DupMask |= set1DupMask;
        set1DupMask = 0;
        set0PortEnableMask |= set1PortEnableMask;
        set1PortEnableMask = 0;
    }

    const uint16_t bitmask        = (1 << m_serverConnectivity.getScaleupNicsMacrosCount(comm)) - 1;
    uint16_t       set0NopDupMask = set0DupMask ^ bitmask;
    uint16_t       set1NopDupMask = set1DupMask ^ bitmask;

    // Check if its worth the cost to use dup mask with each set.
    bool useAggSet0 = true;
    bool useAggSet1 = true;
    if (devicesInSet0.size() <= 1)
    {
        useAggSet0 = false;
    }
    if (devicesInSet1.size() <= 1)
    {
        useAggSet1 = false;
    }

    LOG_HCL_DEBUG(HCL,
                  "set0DupMask={:012b}, set1DupMask={:012b}, set0NopDupMask={:012b}, set1NopDupMask={:012b}, "
                  "set0PortEnableMask={:024b}, set1PortEnableMask={:024b}, "
                  "canMergeSets={}, useAggSet0={}, useAggSet1={}",
                  set0DupMask,
                  set1DupMask,
                  set0NopDupMask,
                  set1NopDupMask,
                  set0PortEnableMask,
                  set1PortEnableMask,
                  canMergeSets,
                  useAggSet0,
                  useAggSet1);

    int savings = 0;

    for (size_t i = 0; i < m_aggEntryArrays.size(); i++)
    {
        const AggregatedEntryArray& arr = m_aggEntryArrays[i];

        DwordsBoxesArray bufferPair0;
        DwordsBoxesArray bufferPair1;

        for (size_t deviceId = 0; deviceId < m_serverConnectivity.getNumberOfDevicesPerHost(); deviceId++)
        {
            if (deviceId == m_selfModuleId) continue;

            const AggregatedEntry& entry = arr[deviceId];
            if (entry.data.isValid)
            {
                // A device can only belong to first or second set, not both, but if we merged the 2 sets into one we
                //  will only use set0
                const bool isSet0 = devicesInSet0.count(deviceId) == 1;

                const bool useAggInSet = isSet0 ? useAggSet0 : useAggSet1;
                LOG_HCL_TRACE(HCL,
                              "deviceId {}: Detected a valid user send/recv, address=0x{:x}, count={}, remoteRank={}, "
                              "isSet0={}, useAggInSet={}",
                              deviceId,
                              entry.data.address,
                              entry.data.count,
                              entry.data.remoteRank,
                              isSet0,
                              useAggInSet);

                // if its "worthy" to use dup mask aggregation for this device
                if (useAggInSet)
                {
                    DwordsBoxesArray& buffer(isSet0 ? bufferPair0 : bufferPair1);

                    m_commands.serializeScaleUpSendRecvDeviceCmd(
                        m_isSend,
                        qpn,
                        entry.data.address,
                        entry.data.count,
                        dcore,
                        ssm,
                        sobId,
                        isSet0 ? set0PortEnableMask
                               : set1PortEnableMask,  // merged port mask for all devices in the set
                        entry.data.dataType,
                        m_serverConnectivity.getMaxNumScaleUpPortsPerConnection(comm),
                        buffer[deviceId]);
                    LOG_HCL_TRACE(HCL,
                                  "Added to aggregation buffer.size()={} DWORDS for deviceId={}",
                                  buffer[deviceId].size(),
                                  deviceId);
                }
                else  // no aggregation - send cmd directly now to device
                {
                    m_commands.serializeScaleUpSendRecvDevice(
                        scalStream,
                        deviceId,
                        m_isSend,
                        qpn,
                        entry.data.address,
                        entry.data.count,
                        dcore,
                        ssm,
                        sobId,
                        m_serverConnectivity.getRemoteDevicesPortMasks(comm)[deviceId],
                        entry.data.dataType,
                        m_serverConnectivity.getMaxNumScaleUpPortsPerConnection(comm));
                }
            }
        }

        // adds new items to records ("new") if aggregating
        if (useAggSet0)
        {
            savings += m_nicPassthroughHandlerSet0.addDeviceBuffer(bufferPair0, devicesInSet0, comm);
        }

        if (useAggSet1)
        {
            savings += m_nicPassthroughHandlerSet1.addDeviceBuffer(bufferPair1, devicesInSet1, comm);
        }
    }

    // flush the commands if using aggregation
    if (useAggSet0)
    {
        savings -= m_nicPassthroughHandlerSet0.flush(scalStream, set0NopDupMask);
    }
    if (useAggSet1)
    {
        savings -= m_nicPassthroughHandlerSet1.flush(scalStream, set1NopDupMask);
    }
    LOG_HCL_TRACE(HCL, "Total saved {} Dwords", savings);

    m_aggEntryArrays.clear();
}
