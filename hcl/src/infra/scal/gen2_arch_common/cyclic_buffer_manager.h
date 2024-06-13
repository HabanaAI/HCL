#pragma once

#include <array>    // for array
#include <cstddef>  // for size_t
#include <cstdint>  // for uint64_t, uint32_t
#include <string>   // for string
#include "scal.h"   // for scal_stream_handle_t, scal_stream_info_t
#include "hl_logger/hllog_core.hpp"  // for hl_logger::LoggerSPtr

class HclCommandsGen2Arch;
namespace hcl
{
class CompletionGroup;
class Gen2ArchScalWrapper;
class ScalJsonNames;
class ScalStreamBase;
}  // namespace hcl

namespace hcl
{

/**
 * @brief
 *
 * CyclicBufferManager is responsible for managing cyclic buffer AKA MicroArchStream.
 * It responsible on adding commands to the buffer, mangaing the pi and alignment.
 * ** FOr now, it not responsible for sending the buffer to the device.
 *
 */
class CyclicBufferManager
{
public:
    CyclicBufferManager(ScalStreamBase*       scalStream,
                        ScalJsonNames&        scalNames,
                        CompletionGroup&      cg,
                        uint64_t              hostAddress,
                        scal_stream_info_t&   streamInfo,
                        uint64_t              bufferSize,  // must be a power of 2
                        std::string&          streamName,
                        scal_stream_handle_t& streamHandle,
                        Gen2ArchScalWrapper&  scalWrapper,
                        unsigned              schedIdx,
                        HclCommandsGen2Arch&  commands);
    CyclicBufferManager(CyclicBufferManager&&)      = delete;
    CyclicBufferManager(const CyclicBufferManager&) = delete;
    CyclicBufferManager& operator=(CyclicBufferManager&&) = delete;
    CyclicBufferManager& operator=(const CyclicBufferManager&) = delete;
    virtual ~CyclicBufferManager()                             = default;

    void        setTargetValue(uint64_t targetValue);
    void*       getNextPtr(size_t size);
    inline bool requiresSubmission() { return m_sizeSinceAlignment > 0; }
    void        submit(bool force = false);
    static bool isACcbHalfFullForDeviceBenchMark();
    void        updateCcbHalfFullMechanism();

    virtual uint64_t getPi() = 0;
    void    disableCcb(bool disable) { m_disableCcb = disable; }
    void    dfaLog(hl_logger::LoggerSPtr synDevFailLog);

    static constexpr unsigned m_numberOfDivisions = 32;

    static bool s_ccbIsFullForDeviceBenchMark;
    static int  s_ccbFillRoundForCurrStream;

protected:
    void advanceAlignment(size_t size);

    virtual void incPi(uint32_t size) = 0;
    void moveToNextDivision();

    ScalStreamBase*           m_scalStream;
    ScalJsonNames&            m_scalNames;
    CompletionGroup&          m_cg;            // Internal cg of the arch stream
    uint64_t                  m_pi;            // We do not save the modulo it will always increase
    const uint64_t            m_bufferSize;    // Cyclic buffer size
    const uint64_t            m_hostAddress;   // Host address given by scal
    uint64_t                  m_hostPi;        // Keeps track of the current dword to write to
    const scal_stream_info_t& m_streamInfo;    // Scal's stream info for commands alignemt and submittion alignemt
    const uint64_t            m_divAlignment;  // SW alignment for mangaing the cyclic buffer
    unsigned                  m_divIndex;      // Current divsion we are on
    std::array<uint64_t, m_numberOfDivisions>
                                          m_targtValueOfBufferChunk;  // the target value of the job of each devision
    std::array<bool, m_numberOfDivisions> m_targtValueOfBufferSet;

    std::string&          m_streamName;  // For Debug
    scal_stream_handle_t& m_streamHandle;
    Gen2ArchScalWrapper&  m_scalWrapper;
    unsigned              m_schedIdx;

    uint64_t m_targetValue         = 0;
    size_t   m_sizeLeftInAlignment = 0;
    size_t   m_sizeSinceAlignment  = 0;
    HclCommandsGen2Arch& m_commands;

    bool     m_disableCcb          = false; // used for null submission
    const uint64_t m_logOfBufferSize;             // Cyclic buffer size
};
}  // namespace hcl
