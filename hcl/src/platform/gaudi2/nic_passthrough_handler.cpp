#include "platform/gaudi2/nic_passthrough_handler.h"

#include <algorithm>  // for max_element
#include <cstdint>    // for uint32_t
#include <cstring>    // for memset, memcpy
#include <map>        // for map
#include <memory>     // for __shared_ptr_access
#include <utility>    // for pair

#include "platform/gaudi2/types.h"                          // for HLS2_BOX_SIZE
#include "g2_sched_pkts.h"                                  // for g2fw
#include "hcl_utils.h"                                      // for VERIFY
#include "hcl_log_manager.h"                                // for LOG_*
#include "platform/gaudi2/commands/hcl_commands.h"          // for HclCommandsGaudi2
#include "platform/gen2_arch_common/types.h"                // for MAX_NICS_GEN2ARCH
#include "platform/gen2_arch_common/server_connectivity.h"  // for Gen2ArchServerConnectivity

class HclCommandsGen2Arch;

NicPassthroughHandler::NicPassthroughHandler(const std::vector<unsigned>&      nicEngines,
                                             const Gen2ArchServerConnectivity& serverConnectivity,
                                             HclCommandsGen2Arch&              commands)
: NicPassthroughHandlerBase(), m_serverConnectivity(serverConnectivity), m_commands((HclCommandsGaudi2&)commands)
{
    memset(m_dupMasksPerDevice, 0, sizeof(m_dupMasksPerDevice));
    memset(m_dupMasksPerNic, 0, sizeof(m_dupMasksPerNic));

    for (unsigned deviceId = 0; deviceId < m_serverConnectivity.getBoxSize(); deviceId++)
    {
        for (unsigned port : serverConnectivity.getAllPortsGlbl(deviceId))
        {
            for (unsigned i = 0; i < nicEngines.size(); i++)
            {
                if (nicEngines[i] == port)
                {
                    m_dupMasksPerDevice[deviceId] |= (1 << i);
                }
            }
        }
    }

    for (unsigned nic = 0; nic < MAX_NICS_GEN2ARCH; nic++)
    {
        for (unsigned i = 0; i < nicEngines.size(); i++)
        {
            if (nicEngines[i] == nic)
            {
                m_dupMasksPerNic[nic] = (1 << i);
                break;
            }
        }
    }
}

uint32_t NicPassthroughHandler::getDupMask(const int deviceId)
{
    return m_dupMasksPerDevice[deviceId];
}

static inline pRecordWithMetadata make_shared()
{
    // C++11 is crap. We don't have std::make_shared(U[]) so we must explicitly specify the destructor to use the
    // correct delete[] notation.

    // Each record is of size sizeof(RecordWithMetadata) + 1 dword for possible 'passthrough_data[1]' field.
    static constexpr size_t sizeInDwords = sizeof(RecordWithMetadata) / sizeof(uint32_t) + 1;

    return pRecordWithMetadata(reinterpret_cast<RecordWithMetadata*>(new uint32_t[sizeInDwords]()),
                               [](RecordWithMetadata* r) { delete[] r; });
}

void NicPassthroughHandler::pushToRecordVector(uint32_t dupMask, uint32_t payload)
{
    size_t currentIndex = m_records.size() - 1;
    bool   newRecord    = false;

    std::vector<pRecordWithMetadata>&                  records = m_records[currentIndex];
    std::vector<pRecordWithMetadata>::reverse_iterator it      = records.rbegin();

    if (records.size() == 0 || (*it)->data.num_payload_dwords == 1 || (*it)->data.dup_mask != dupMask)
    {
        // Either recordBuff is empty, the last record is full or the dup_mask is different. Add another record.
        records.push_back(make_shared());
        newRecord = true;
    }

    pRecordWithMetadata& record = (*records.rbegin());

    record->graphIndex          = currentIndex;
    record->data.dup_mask       = dupMask;
    record->data.is_last_config = 0;  // will set this manually on the last record of the group
    record->data.is_nop         = 0;

    if (newRecord)
    {
        memcpy(&record->data.payload0, &payload, sizeof(payload));
        record->data.num_payload_dwords = 0;
    }
    else
    {
        size_t    offset = offsetof(RecordWithMetadata, data) + offsetof(g2fw::nic_passthrough_data_t, payload1);
        uint32_t* ptr    = ((uint32_t*)record.get()) + (offset / sizeof(uint32_t));
        *ptr             = payload;
        record->data.num_payload_dwords = 1;
        LOG_HCL_TRACE(HCL, "Successfully aggregated a record for dupMask 0x{:x}", dupMask);
    }
}

void NicPassthroughHandler::addNicBuffer(const NicsDwordsArray& nicBuffer)
{
    // Add a new empty std::vector<pRecordWithMetadata> for this Buffer to use
    m_records.emplace_back();

    std::vector<UnionFindNode> roots;
    for (unsigned dword = 0; dword < g2fw::ARC_CMD_SEND_RECV_SHORT_SIZE_DWORD; dword++)
    {
        UnionFind forest(MAX_NICS_GEN2ARCH);
        for (size_t nic = 0; nic < MAX_NICS_GEN2ARCH; nic++)
        {
            // Skip if this command doesn't have enough dwords.
            if (m_dupMasksPerNic[nic] == 0) continue;
            if (dword >= nicBuffer[nic].size()) continue;
            forest.addNode(nicBuffer[nic][dword], m_dupMasksPerNic[nic]);
        }

        std::vector<UnionFindNode> result = forest.getRoots();
        LOG_HCL_TRACE(HCL, "Found {} different required records for dword {}", result.size(), dword);
        roots.insert(roots.end(), result.begin(), result.end());
    }

    int i = 0;
    for (const UnionFindNode& root : roots)
    {
        LOG_HCL_TRACE(HCL, "Record {}: 0x{:0>8x}\t(mask 0x{:x})", ++i, root.m_value, root.m_dupMask);
        pushToRecordVector(root.m_dupMask, root.m_value);
    }
}

void NicPassthroughHandler::addDeviceBuffer(const DwordsBoxesArray& deviceBuffer, const HCL_Comm comm)
{
    NicsDwordsArray nicBuffer;

    for (size_t deviceId = 0; deviceId < deviceBuffer.size(); deviceId++)
    {
        for (unsigned nic : m_serverConnectivity.getAllPortsGlbl(deviceId, comm))
        {
            for (const uint32_t val : deviceBuffer[deviceId])
            {
                nicBuffer[nic].push_back(val);
                LOG_HCL_TRACE(HCL, "comm={}, Adding DWORD deviceId={}, nic={}, val=0x{:x}", comm, deviceId, nic, val);
            }
        }
    }

    addNicBuffer(nicBuffer);
}

RecordsPerCommands NicPassthroughHandler::coalesceRecords(RecordsPerCommands& records)
{
    RecordsPerCommands result;
    if (records.size() == 1)
    {
        result = records;
    }

    for (size_t i = 0; i < records.size() - 1; i++)
    {
        result.push_back(records[i]);

        size_t expectedSize =
            1 + m_commands.recordsSizeInDwords(records[i]) + m_commands.recordsSizeInDwords(records[i + 1]);
        if (expectedSize <= 14)  // maximum size - 16 dwords. reserve 2 dwords for NOPs.
        {
            LOG_HCL_INFO(HCL, "Coalescing records for arrays {}, {} to a single passthrough command", i, i + 1);
            const auto& last = result.rbegin();
            const auto& next = records[i + 1];
            last->insert(last->end(), next.begin(), next.end());
            i++;
        }
        else if (i + 1 == records.size() - 1)  // this next entry is the last one - need to make sure it's in
        {
            result.push_back(records[i + 1]);
        }
    }

    return result;
}

std::array<int, HLS2_BOX_SIZE> NicPassthroughHandler::getCreditsPerDevice(std::vector<pRecordWithMetadata>& records)
{
    std::array<int, HLS2_BOX_SIZE> creditsPerDevice {0};

    for (pRecordWithMetadata& record : records)
    {
        for (unsigned deviceId = 0; deviceId < m_serverConnectivity.getBoxSize(); deviceId++)
        {
            if ((record->data.dup_mask & m_dupMasksPerDevice[deviceId]) == 0) continue;

            if (record->data.is_nop)
            {
                g2fw::arc_cmd_nic_send_recv_nop_t* command =
                    (g2fw::arc_cmd_nic_send_recv_nop_t*)(&record->data.payload0);
                creditsPerDevice[deviceId] += command->queue_credits_bytes;
            }
            else
            {
                uint32_t creditsInDwords = (record->data.num_payload_dwords == 0 ? 1 : 2);
                creditsPerDevice[deviceId] += creditsInDwords * sizeof(uint32_t);
            }
        }
    }

    return creditsPerDevice;
}

size_t NicPassthroughHandler::recordsCreditsInDwords(std::vector<pRecordWithMetadata>& records)
{
    std::array<int, HLS2_BOX_SIZE> creditsPerDevice = getCreditsPerDevice(records);

    int storedCredits = 0;
    for (unsigned deviceId = 0; deviceId < m_serverConnectivity.getBoxSize(); deviceId++)
    {
        if (creditsPerDevice[deviceId] == 0) continue;

        if (storedCredits == 0)
        {
            storedCredits = creditsPerDevice[deviceId];
        }
        else
        {
            VERIFY(storedCredits == creditsPerDevice[deviceId],
                   "stored credits = {} but deviceId {} has {} credits!",
                   storedCredits,
                   deviceId,
                   creditsPerDevice[deviceId]);
        }
    }

    return storedCredits;
}

void NicPassthroughHandler::fillInNicNops(std::vector<pRecordWithMetadata>& records,
                                          unsigned                          collectiveContextIndex,
                                          int                               selfModuleId,
                                          unsigned                          syncObjectAddressIndex,
                                          bool                              isLast,
                                          bool                              incSOBinNOP)
{
    std::array<int, HLS2_BOX_SIZE> creditsPerDevice = getCreditsPerDevice(records);
    std::map<int, uint32_t>        creditsForMask;

    int maxCredits = *std::max_element(creditsPerDevice.begin(), creditsPerDevice.end());

    if (maxCredits == 0)
    {
        LOG_HCL_INFO(HCL, "Adding a NIC NOP for an empty send/recv for collectiveContext({})", collectiveContextIndex);

        uint32_t dupMask = 0;
        for (unsigned deviceId = 0; deviceId < m_serverConnectivity.getBoxSize(); deviceId++)
        {
            dupMask |= m_dupMasksPerDevice[deviceId];
        }

        records.push_back(make_shared());
        m_commands.serializeNicNopCommand(*records.rbegin(),
                                          collectiveContextIndex,
                                          dupMask,
                                          sizeof(uint32_t),
                                          syncObjectAddressIndex,
                                          isLast && incSOBinNOP);
        return;
    }

    for (unsigned deviceId = 0; deviceId < m_serverConnectivity.getBoxSize(); deviceId++)
    {
        if (deviceId == (unsigned)selfModuleId) continue;

        int missingCredits = maxCredits - creditsPerDevice[deviceId];
        if (missingCredits > 0)
        {
            // Need to add credits to deviceId! Coalesce with other deviceIds.
            creditsForMask[missingCredits] |= m_dupMasksPerDevice[deviceId];
        }
    }

    for (std::pair<int, uint32_t> creditForMask : creditsForMask)
    {
        int      missingCredits = creditForMask.first;
        uint32_t dupMaskForNop  = creditForMask.second;

        LOG_HCL_INFO(HCL,
                     "Adding a NIC NOP for send/recv for collectiveContext({}) with dupMask 0x{:x} and {} credits",
                     collectiveContextIndex,
                     dupMaskForNop,
                     missingCredits);

        records.push_back(make_shared());
        m_commands.serializeNicNopCommand(*records.rbegin(),
                                          collectiveContextIndex,
                                          dupMaskForNop,
                                          missingCredits,
                                          syncObjectAddressIndex,
                                          isLast && incSOBinNOP);
    }
}

void NicPassthroughHandler::flush(hcl::ScalStreamBase&      scalStream,
                                  unsigned                  collectiveContextIndex,
                                  int                       selfModuleId,
                                  [[maybe_unused]] HCL_Comm comm,
                                  unsigned                  syncObjectAddressIndex,
                                  bool                      isSend,
                                  bool                      incSOBinNOP)
{
    RecordsPerCommands recordsPerCommand = coalesceRecords(m_records);
    for (unsigned i = 0; i < recordsPerCommand.size(); i++)
    {
        bool isLast = i == recordsPerCommand.size() - 1;

        fillInNicNops(recordsPerCommand[i],
                      collectiveContextIndex,
                      selfModuleId,
                      syncObjectAddressIndex,
                      isLast,
                      incSOBinNOP);
        m_commands.serializeNicPassthroughCommand(scalStream,
                                                  recordsPerCommand[i],
                                                  recordsCreditsInDwords(recordsPerCommand[i]),
                                                  isSend);
    }

    m_records.clear();
}
