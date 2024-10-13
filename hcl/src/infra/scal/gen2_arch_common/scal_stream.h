#pragma once

#include <cstddef>                   // for size_t
#include <cstdint>                   // for uint32_t, uint64_t
#include <memory>                    // for unique_ptr
#include <string>                    // for string
#include <vector>                    // for vector
#include "scal.h"                    // for scal_buffer_handle_t, scal_buffer_info_t, scal_s...
#include "hl_logger/hllog_core.hpp"  // for hl_logger::LoggerSPtr
#include "infra/scal/gen2_arch_common/cyclic_buffer_factory.h"  // for CyclicBufferFactory
#include "infra/scal/gaudi_common/factory_types.h"              // for CyclicBufferType

namespace hcl
{
class CompletionGroup;
class CyclicBufferManager;
class Gen2ArchScalWrapper;
class ScalJsonNames;
}  // namespace hcl

class HclCommandsGen2Arch;

namespace hcl
{
/**
 * @brief
 *
 * ScalStreamBase is responsible for encapsulating a buffer. It's used by hcl_packets.h mainly because the
 * some clients need the actual buffer (ContextManager, NicPassthroughHandler, unit tests).
 */
class ScalStreamBase
{
public:
    ScalStreamBase()          = default;
    virtual ~ScalStreamBase() = default;

    virtual void*        getNextPtr(size_t size);
    virtual std::string* getStreamName() { return &m_defaultStream; };

    std::vector<uint32_t> m_buffer;
    std::string           m_defaultStream = "unknown_stream";
};
/**
 * @brief
 *
 *  ScalStream responsible for managing a cyclic buffer for a given stream name.
 */
class ScalStream : public ScalStreamBase
{
public:
    ScalStream(ScalJsonNames&       scalNames,
               const std::string&   schedNameAndStreamNum,
               const std::string&   schedAndStreamName,
               Gen2ArchScalWrapper& scalWrapper,
               CompletionGroup&     cg,
               unsigned             schedIdx,
               unsigned             internalStreamIdx,
               unsigned             archStreamIndex,
               HclCommandsGen2Arch& commands,
               CyclicBufferType     type);
    ScalStream(ScalStream&&)                 = delete;
    ScalStream(const ScalStream&)            = delete;
    ScalStream& operator=(ScalStream&&)      = delete;
    ScalStream& operator=(const ScalStream&) = delete;
    virtual ~ScalStream();

    void setTargetValue(uint64_t targetValue);

    virtual void* getNextPtr(size_t size) override;

    bool        requiresSubmission();
    void        submit();
    static bool isACcbHalfFullForDeviceBenchMark();
    void        disableCcb(bool disable);
    void        dfaLog(hl_logger::LoggerSPtr synDevFailLog);

    inline unsigned        getStreamIndex() { return m_internalStreamIdx; };
    inline unsigned        getSchedIdx() { return m_schedIdx; };
    inline unsigned        getArchStreamIndex() { return m_archStreamIndex; }
    static inline unsigned getCcbSize() { return m_hostCyclicBufferSize; }
    virtual std::string*   getStreamName() override { return &m_schedNameAndStreamNum; }
    virtual std::string*   getSchedAndStreamName() { return &m_schedAndStreamName; }

private:
    const ScalJsonNames&  m_scalNames;
    static const uint64_t m_core_counter_max_value = 1 << 16;
    static const uint64_t piQuant                  = 4;

protected:
    scal_stream_handle_t m_streamHandle;
    scal_stream_info_t   m_streamInfo;
    scal_buffer_handle_t m_bufferHandle;
    scal_buffer_info_t   m_bufferInfo;
    Gen2ArchScalWrapper& m_scalWrapper;
    std::string          m_schedNameAndStreamNum;  // For Debug
    std::string          m_schedAndStreamName;     // For Debug
    unsigned             m_schedIdx;
    unsigned             m_internalStreamIdx;  // this is equal to streamIdx in m_streams[schedIdx][streamIdx]
    unsigned             m_archStreamIndex;

    static const uint64_t m_hostCyclicBufferSize = m_core_counter_max_value * piQuant;

    std::unique_ptr<CyclicBufferManager> m_cyclicBuffer;
};
}  // namespace hcl
