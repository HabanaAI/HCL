#pragma once

#include <chrono>
#include <cstdint>

#include "infra/hcl_spsc_fifo.h"
#include "hccl_internal_defs.h"

const uint32_t                            HOST_STREAM_CAPACITY = 1024 * 1024 /*TODO - TBD*/;
typedef spsc_fifo_t<HOST_STREAM_CAPACITY> HostStreamFifo;
using spHostStreamFifo = std::shared_ptr<HostStreamFifo>;

enum HostStreamType
{
    HOST_STREAM_SEND               = 0,
    HOST_STREAM_RECV               = 1,
    HOST_STREAM_WAIT_FOR_SEND_COMP = 2,
    HOST_STREAM_WAIT_FOR_RECV_COMP = 3,
    NUM_HOST_STREAMS
};

struct innerQueueMsg
{
    hcclOfiHandle handle;
    uint64_t            submitTime =
        0;  // For debug, time when message was put into queue, used by consuming wait for completion stream
    uint64_t srCount =
        0;  // For debug, s/r ops counter when msg submitted, used by consuming wait for completion stream
};

class HostStream
{
public:
    HostStream(const std::string& name,
               unsigned           archStreamIdx,
               unsigned           uarchStreamIdx,
               spHostStreamFifo   innerQueue,
               HostStreamType     type);
    virtual ~HostStream() = default;

    HostStream(HostStream&)  = delete;
    HostStream(HostStream&&) = delete;
    HostStream&  operator=(HostStream&) = delete;
    HostStream&& operator=(HostStream&&) = delete;

    spHostStreamFifo   getOuterQueue() { return m_outerQueue; }
    spHostStreamFifo   getInnerQueue() { return m_innerQueue; }
    const std::string& getStreamName() const { return m_streamName; }
    const unsigned     getArchStreamIdx() const { return m_archStreamIdx; }
    const unsigned     getUarchStreamIdx() const { return m_uarchStreamIdx; }
    bool               isEmpty();

    inline uint64_t getCurrentSrCountProcessing() const { return m_currentSrCountProcessing; }
    inline void     setCurrentSrCountProcessing(uint64_t newSrCount) { m_currentSrCountProcessing = newSrCount; }

    // for Debug
    inline bool        getOnGoingProcessing() const { return m_ongoingProcessing; }
    inline void        setOnGoingProcessing(bool isOngoing) { m_ongoingProcessing = isOngoing; }
    inline std::string getOnGoingFuncName() const { return m_funcName; }
    inline void        setOnGoingFuncName(std::string funcName) { m_funcName = funcName; }
    const HostStreamType& getType() const { return m_type; }

    inline uint64_t getCurrTimeMsec() const
    {
        const auto now    = std::chrono::steady_clock::now();
        const auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
        const auto epoch  = now_ms.time_since_epoch();
        const auto value  = std::chrono::duration_cast<std::chrono::milliseconds>(epoch);
        return value.count();
    }

    inline uint64_t getSrCount() const { return m_srCount; }  // used by s/r submit stream
    inline void     incSrCount() { m_srCount++; }             // used by s/r submit stream

private:
    std::string      m_streamName;  // For Debug
    spHostStreamFifo m_innerQueue;  // For passing info between 2 host streams (Example: ofi_req)
    spHostStreamFifo m_outerQueue;
    unsigned         m_archStreamIdx;
    unsigned         m_uarchStreamIdx;
    const HostStreamType m_type;

    // for Debug
    bool                                  m_timerStarted;
    std::chrono::steady_clock::time_point m_startTime;
    std::chrono::steady_clock::time_point m_endTime;

    uint64_t m_srCount = 0;  // For debug, counts s/r ops in host main thread, transferred to scheduler thread

    bool                                  m_ongoingProcessing = false;
    std::string                           m_funcName;

    uint64_t m_currentSrCountProcessing = 0;
};