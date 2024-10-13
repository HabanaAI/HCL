#pragma once

#include <cstddef>  // for size_t
#include <cstdint>  // for uint32_t
#include <array>    // for array
#include <vector>   // for vector
#include <memory>   // for shared_ptr

#include "hcl_api_types.h"                                           // for HCL_Comm
#include "sched_pkts.h"                                              // for g3fw
#include "gaudi3/gaudi3_arc_sched_packets.h"                         // for g3fw::sched_arc_cmd_nic_passthrough_v2_t
#include "platform/gen2_arch_common/nic_passthrough_handler_base.h"  // for NicPassthroughHandlerBase
#include "gaudi3/nic_patcher_cmds.h"  // for direct_coll_desc_send_receive, coll_desc_consume_space
#include "platform/gen2_arch_common/server_connectivity_types.h"  // for DEFAULT_COMM_ID
#include "platform/gaudi3/nic_macro_types.h"                      // for DevicesSet

namespace hcl
{
class ScalStreamBase;
}
class HclCommandsGaudi3;
class Gaudi3BaseServerConnectivity;

static constexpr size_t PAYLOAD_LEN_DWORDS = sizeof(gaudi3::Nic::direct_coll_desc_send_receive) / sizeof(uint32_t);

struct RecordWithMetadataGaudi3
{
    uint16_t m_numDwords                   = 0;
    uint16_t m_dupMask                     = 0;  // dup mask of 12 bits,1 bit per nic macro pair
    uint32_t m_payload[PAYLOAD_LEN_DWORDS] = {};
};

using pRecordWithMetadataGaudi3 = std::shared_ptr<RecordWithMetadataGaudi3>;

using RecordsPerCommandsGaudi3 = std::vector<std::vector<pRecordWithMetadataGaudi3>>;

class NicPassthroughHandlerGaudi3 : public NicPassthroughHandlerBase
{
public:
    NicPassthroughHandlerGaudi3(const bool                          isSend,
                                const bool                          isSet0,
                                const Gaudi3BaseServerConnectivity& serverConnectivity,
                                HclCommandsGaudi3&                  commands);
    virtual ~NicPassthroughHandlerGaudi3()                                     = default;
    NicPassthroughHandlerGaudi3(NicPassthroughHandlerGaudi3&&)                 = delete;
    NicPassthroughHandlerGaudi3(const NicPassthroughHandlerGaudi3&)            = delete;
    NicPassthroughHandlerGaudi3& operator=(NicPassthroughHandlerGaudi3&&)      = delete;
    NicPassthroughHandlerGaudi3& operator=(const NicPassthroughHandlerGaudi3&) = delete;

    virtual void addNicBuffer(const NicsDwordsArray& nicBuffer) override;  // for nic macro pair
    // Add all devices vectors of dwords to internal structures,
    // creating a vector of dwords per nic per device, for all devices
    // in the set returns number of dwords used before aggregation
    // Unit tests use default comm id
    int addDeviceBuffer(const DwordsBoxesArray& deviceBuffer,
                        const DevicesSet&       devicesSet,
                        const HCL_Comm          comm = DEFAULT_COMM_ID);
    // returns number of dwords used after aggregation
    int flush(hcl::ScalStreamBase& scalStream, const uint16_t setNopDupMask);

private:
    void                     pushToRecordVector(const uint32_t dupMask, const uint32_t payload);
    RecordsPerCommandsGaudi3 coalesceRecords(RecordsPerCommandsGaudi3& records);
    void                     clearAfterSerialize();
    int fillInNicNops(hcl::ScalStreamBase& scalStream, const uint32_t consumeDwords, const uint16_t setNopDupMask);

    static constexpr size_t NOP_LEN_DWORDS    = sizeof(gaudi3::Nic::coll_desc_consume_space) / sizeof(uint32_t);
    static constexpr size_t HEADER_LEN_DWORDS = sizeof(g3fw::sched_arc_cmd_nic_passthrough_v2_t) / sizeof(uint32_t);

    const bool                          m_isSend;
    const bool                          m_isSet0;
    const Gaudi3BaseServerConnectivity& m_serverConnectivity;
    RecordsPerCommandsGaudi3            m_records;
    HclCommandsGaudi3&                  m_commands;
};
