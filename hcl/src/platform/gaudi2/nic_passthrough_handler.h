#pragma once

#include <cstddef>  // for size_t
#include <cstdint>  // for uint32_t
#include <array>    // for array
#include <vector>   // for vector
#include <memory>   // for shared_ptr

#include "hcl_api_types.h"                                           // for HCL_Comm
#include "platform/gaudi2/types.h"                                   // for HLS2_BOX_SIZE
#include "platform/gen2_arch_common/types.h"                         // for MAX_NICS_GEN2ARCH, GEN2ARCH_HLS_BOX_SIZE
#include "g2_sched_pkts.h"                                           // for g2fw
#include "platform/gen2_arch_common/nic_passthrough_handler_base.h"  // for NicPassthroughHandlerBase

class HclCommandsGen2Arch;
class ContextManager;
class Gen2ArchServerConnectivity;

namespace hcl
{
class ScalStreamBase;
}
class HclCommandsGaudi2;

struct RecordWithMetadata
{
    unsigned                            graphIndex;
    struct RecordWithMetadata*          next;
    struct g2fw::nic_passthrough_data_t data;
};

using pRecordWithMetadata = std::shared_ptr<RecordWithMetadata>;

using RecordsPerCommands = std::vector<std::vector<pRecordWithMetadata>>;

class NicPassthroughHandler : public NicPassthroughHandlerBase
{
public:
    // nicEngines is the return value of hcl::ScalManager->getNicsScaleUpEngines();
    NicPassthroughHandler(const std::vector<unsigned>&      nicEngines,
                          const Gen2ArchServerConnectivity& serverConnectivity,
                          HclCommandsGen2Arch&              commands);
    virtual ~NicPassthroughHandler() = default;

    static_assert(GEN2ARCH_HLS_BOX_SIZE == HLS2_BOX_SIZE, "G2 must match Gen2Arch box size");

    virtual uint32_t getDupMask(const int deviceId);

    void addNicBuffer(const NicsDwordsArray& nicBuffer) override;
    void addDeviceBuffer(const DwordsBoxesArray& deviceBuffer, const HCL_Comm comm);

    void flush(hcl::ScalStreamBase& scalStream,
               unsigned             collectiveContextIndex,
               int                  selfModuleId,
               HCL_Comm             comm,
               unsigned             syncObjectAddressIndex,
               bool                 isSend,
               bool                 incSOBinNOP = true);

private:
    void fillInNicNops(std::vector<pRecordWithMetadata>& records,
                       unsigned                          collectiveContextIndex,
                       int                               selfModuleId,
                       unsigned                          syncObjectAddressIndex,
                       bool                              isLast,
                       bool                              incSOBinNOP);

    void configureLastEntriesPerDevice(RecordsPerCommands& recordsPerCommand);

    RecordsPerCommands coalesceRecords(RecordsPerCommands& records);

    std::array<int, HLS2_BOX_SIZE> getCreditsPerDevice(std::vector<pRecordWithMetadata>& records);

    size_t recordsCreditsInDwords(std::vector<pRecordWithMetadata>& records);

    void pushToRecordVector(uint32_t dupMask, uint32_t payload);

    const Gen2ArchServerConnectivity& m_serverConnectivity;

    uint32_t m_dupMasksPerNic[MAX_NICS_GEN2ARCH];
    uint32_t m_dupMasksPerDevice[HLS2_BOX_SIZE];

    RecordsPerCommands m_records;
    HclCommandsGaudi2& m_commands;
};
