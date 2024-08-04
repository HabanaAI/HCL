#include "platform/gaudi3/nic_passthrough_handler.h"

#include <cstdint>  // for uint32_t
#include <cstring>  // for memset, memcpy
#include <memory>   // for shared_ptr
#include <utility>  // for pair, make_pair

#include "sched_pkts.h"                              // for g3fw
#include "hcl_utils.h"                               // for VERIFY
#include "hcl_log_manager.h"                         // for LOG_*
#include "platform/gaudi3/commands/hcl_commands.h"   // for HclCommandsGaudi3
#include "platform/gen2_arch_common/port_mapping.h"  // for Gen2ArchDevicePortMapping
#include "gaudi3/gaudi3.h"                           // for NIC_MAX_NUM_OF_MACROS
#include "platform/gaudi3/port_mapping.h"            // for DeviceNicsMacrosMask, NicMacrosDevicesArray

NicPassthroughHandlerGaudi3::NicPassthroughHandlerGaudi3(const bool                     isSend,
                                                         const bool                     isPair0,
                                                         const Gaudi3DevicePortMapping& portMapping,
                                                         HclCommandsGaudi3&             commands)
: NicPassthroughHandlerBase(), m_isSend(isSend), m_isSet0(isPair0), m_portMapping(portMapping), m_commands(commands)
{
}

static inline pRecordWithMetadataGaudi3 make_shared()
{
    // We don't have std::make_shared(U[]) so we must explicitly specify the destructor to use the
    // correct delete[] notation.

    // Each record is of size sizeof(RecordWithMetadataGaudi3)
    constexpr size_t sizeInDwords = sizeof(RecordWithMetadataGaudi3) / sizeof(uint32_t);

    return pRecordWithMetadataGaudi3(reinterpret_cast<RecordWithMetadataGaudi3*>(new uint32_t[sizeInDwords]()),
                                     [](RecordWithMetadataGaudi3* r) { delete[] r; });
}

void NicPassthroughHandlerGaudi3::pushToRecordVector(const uint32_t dupMask, const uint32_t payload)
{
    const size_t currentIndex = m_records.size() - 1;
    bool         newRecord    = false;

    std::vector<pRecordWithMetadataGaudi3>&                  records = m_records[currentIndex];
    std::vector<pRecordWithMetadataGaudi3>::reverse_iterator it      = records.rbegin();

    if (records.size() == 0 || (*it)->m_dupMask != dupMask)
    {
        // Either recordBuff is empty, the last record is full or the dup_mask is different. Add another record.
        records.push_back(make_shared());
        newRecord = true;
    }
    pRecordWithMetadataGaudi3& record = (*records.rbegin());

    record->m_dupMask = dupMask;

    if (newRecord)
    {
        record->m_payload[0] = payload;  // copy first payload
        record->m_numDwords  = 1;
        LOG_HCL_TRACE(HCL,
                      "Created new record={:p}, for dupMask={:012b}, payload=0x{:x}",
                      record.get(),
                      dupMask,
                      payload);
    }
    else
    {
        record->m_payload[record->m_numDwords++] = payload;
        LOG_HCL_TRACE(HCL,
                      "Successfully aggregated a record={:p}, for dupMask={:012b}, payload=0x{:x}, num_dwords={}",
                      record.get(),
                      dupMask,
                      payload,
                      record->m_numDwords);
    }
}

void NicPassthroughHandlerGaudi3::addNicBuffer(const NicsDwordsArray& nicBuffer)
{
    LOG_HCL_TRACE(HCL, "m_isSend={}, m_isSet0={}", m_isSend, m_isSet0);
    // Add a new empty std::vector<pRecordWithMetadataGaudi3> for this Buffer to use
    m_records.emplace_back();

    std::vector<UnionFindNode> roots;
    constexpr size_t           numDwords = PAYLOAD_LEN_DWORDS;
    for (size_t dword = 0; dword < numDwords; dword++)
    {
        UnionFind forest(nicBuffer.size());
        for (size_t nic = 0; nic < nicBuffer.size(); nic++)
        {
            if (nicBuffer[nic].size() == 0) continue;  // This nic macro does not participate, skip it
            VERIFY(nicBuffer[nic].size() == numDwords,
                   "Nic {} have {} dwords instead of {} dwords",
                   nic,
                   nicBuffer[nic].size(),
                   numDwords);
            forest.addNode(nicBuffer[nic][dword], (1 << nic));
        }

        std::vector<UnionFindNode> result = forest.getRoots();
        LOG_HCL_TRACE(HCL, "Found {} different required records for dword {}", result.size(), dword);
        roots.insert(roots.end(), result.begin(), result.end());
    }

    size_t i = 0;
    for (const UnionFindNode& root : roots)
    {
        LOG_HCL_TRACE(HCL, "Record {}: 0x{:0>8x}\t(mask {:012b}", ++i, root.m_value, root.m_dupMask);
        pushToRecordVector(root.m_dupMask, root.m_value);
    }
}

int NicPassthroughHandlerGaudi3::addDeviceBuffer(const DwordsBoxesArray& deviceBuffer, const DevicesSet& devicesSet)
{
    int usedDwords = 0;
    if (deviceBuffer.size() == 0) return 0;
    LOG_HCL_TRACE(HCL,
                  "m_isSend={}, m_isSet0={}, Adding buffers of DWORDS for up to {} devices",
                  m_isSend,
                  m_isSet0,
                  deviceBuffer.size());

    NicsDwordsArray nicBuffer;

    for (size_t deviceId = 0; deviceId < deviceBuffer.size(); deviceId++)
    {
        if (devicesSet.count(deviceId))  // device belongs to our set
        {
            LOG_HCL_TRACE(HCL,
                          "Checking DWORD for deviceId={}, vector size={}",
                          deviceId,
                          deviceBuffer[deviceId].size());

            // each device belongs to 2 or more NIC macros, in a vector
            const NicMacrosPerDevice& macros(m_portMapping.getNicMacrosPerDevice(deviceId));

            if (deviceBuffer[deviceId].size() > 0)
            {
                for (const uint32_t val : deviceBuffer[deviceId])
                {
                    for (const NicMacroIndexType macro : macros)
                    {
                        nicBuffer[macro].push_back(val);
                    }
                    LOG_HCL_TRACE(HCL,
                                  "Adding DWORD deviceId={}, to macros={}, val=0x{:x}",
                                  deviceId,
                                  macros.size(),
                                  val);
                }
                usedDwords += deviceBuffer[deviceId].size() + HEADER_LEN_DWORDS;  // add full cmd (8 dwords) + 1 header
            }
        }
    }

    addNicBuffer(nicBuffer);
    LOG_HCL_TRACE(HCL, "Returning usedDwords={}", usedDwords);
    return usedDwords;
}

RecordsPerCommandsGaudi3 NicPassthroughHandlerGaudi3::coalesceRecords(RecordsPerCommandsGaudi3& records)
{
    LOG_HCL_TRACE(HCL, "m_isSend={}, m_isSet0={}, records.size={}", m_isSend, m_isSet0, records.size());
    RecordsPerCommandsGaudi3 result;
    if (records.size() == 1)
    {
        result = records;
    }

    for (size_t i = 0; i < records.size() - 1; i++)
    {
        result.push_back(records[i]);
    }

    return result;
}

int NicPassthroughHandlerGaudi3::fillInNicNops(hcl::ScalStreamBase& scalStream,
                                               const uint32_t       consumeDwords,
                                               const uint16_t       setNopDupMask)
{
    const uint32_t credits       = 0;                                // consumeDwords * sizeof(uint32_t);
    const uint16_t dupMaskForNop = setNopDupMask & ((1 << 11) - 1);
    LOG_HCL_DEBUG(HCL,
                  "m_isSend={}, m_isSet0={}: Adding a NIC NOP for send/recv for with dupMask {:012b} and {} credits, "
                  "consumeDwords={}",
                  m_isSend,
                  m_isSet0,
                  dupMaskForNop,
                  credits,
                  consumeDwords);
    m_commands.serializeNicNopCommand(scalStream, m_isSend, dupMaskForNop, credits, consumeDwords);
    return (NOP_LEN_DWORDS + HEADER_LEN_DWORDS);
}

int NicPassthroughHandlerGaudi3::flush(hcl::ScalStreamBase& scalStream, const uint16_t setNopDupMask)
{
    LOG_HCL_DEBUG(HCL, "m_isSend={}, m_isSet0={}, setNopDupMask={:012b}", m_isSend, m_isSet0, setNopDupMask);
    RecordsPerCommandsGaudi3 recordsPerCommand = coalesceRecords(m_records);

    // There is an assumption here that we only have 1 s/r cmd per device
    // The number of credits is calculated to reflect this.
    VERIFY(recordsPerCommand.size() == 1);

    const std::vector<pRecordWithMetadataGaudi3>& records = recordsPerCommand[0];
    uint32_t                                      credits =
        PAYLOAD_LEN_DWORDS *
        sizeof(uint32_t);  // first dup mask record must take credits for the entire engine group, rest are 0

    int usedDwords = 0;
    for (size_t i = 0; i < records.size(); i++)
    {
        VERIFY(records[i] != nullptr);

        pRecordWithMetadataGaudi3 record = std::move(records[i]);  // record will be destroyed after scope
        LOG_HCL_DEBUG(HCL, " Record {}: m_numDwords={}\t(mask={:012b})", i, record->m_numDwords, record->m_dupMask);
        usedDwords += (record->m_numDwords + HEADER_LEN_DWORDS);
        m_commands.serializeNicPassthroughCommand(scalStream, m_isSend, credits, record);
        credits = 0;  // zero credits for all other passthrough commands
    }
    usedDwords += fillInNicNops(scalStream, PAYLOAD_LEN_DWORDS, setNopDupMask);
    clearAfterSerialize();

    LOG_HCL_TRACE(HCL, "Returning usedDwords={}", usedDwords);
    return usedDwords;
}

void NicPassthroughHandlerGaudi3::clearAfterSerialize()
{
    m_records.clear();
}
