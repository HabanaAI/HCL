#pragma once

#include <cstdint>  // for uint64_t, uint16_t
#include <array>    // for array
#include <ostream>  // for ostream
#include <vector>   // for vector

#include "platform/gen2_arch_common/types.h"  // for GEN2ARCH_HLS_BOX_SIZE
#include "hccl_types.h"                       // for hcclDataType_t
#include "hcl_log_manager.h"                  // for hl_logger macros

struct SendRecvEntry
{
    uint64_t       address    = 0;
    uint64_t       count      = 0;
    hcclDataType_t dataType   = hcclNumTypes;
    bool           isValid    = false;
    uint16_t       remoteRank = 0;
};

typedef std::array<SendRecvEntry, GEN2ARCH_HLS_BOX_SIZE> SendRecvArray;
typedef std::vector<SendRecvEntry>                       SendRecvVector;

std::ostream& operator<<(std::ostream& os, const SendRecvEntry& entry);
HLLOG_DEFINE_OSTREAM_FORMATTER(SendRecvEntry);

std::ostream& operator<<(std::ostream& os, const SendRecvVector& sendRecvVector);
HLLOG_DEFINE_OSTREAM_FORMATTER(SendRecvVector);

std::ostream& operator<<(std::ostream& os, const SendRecvArray& sendRecvArray);
HLLOG_DEFINE_OSTREAM_FORMATTER(SendRecvArray);

struct AggregatedEntry
{
    SendRecvEntry data;
    bool          isNop;
    bool          isLast;
};

typedef std::array<AggregatedEntry, GEN2ARCH_HLS_BOX_SIZE> AggregatedEntryArray;

class SendRecvAggregatorBase
{
public:
    SendRecvAggregatorBase()                                         = default;
    virtual ~SendRecvAggregatorBase()                                = default;
    SendRecvAggregatorBase(SendRecvAggregatorBase&&)                 = delete;
    SendRecvAggregatorBase(const SendRecvAggregatorBase&)            = delete;
    SendRecvAggregatorBase& operator=(SendRecvAggregatorBase&&)      = delete;
    SendRecvAggregatorBase& operator=(const SendRecvAggregatorBase&) = delete;

    virtual bool willFlush();

protected:
    std::vector<AggregatedEntryArray> m_arrays;
};
