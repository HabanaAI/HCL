#include "cyclic_buffer_manager.h"

#include <cstdint>                                            // for uint64_t
#include <string>                                             // for string
#include "completion_group.h"                                 // for Complet...
#include "hcl_utils.h"                                        // for LOG_HCL...
#include "infra/scal/gen2_arch_common/scal_wrapper.h"         // for Gen2Arc...
#include "hcl_log_manager.h"                                  // for LOG_*
#include "platform/gen2_arch_common/commands/hcl_commands.h"  // for HclComm...
class ScalStreamBase;

using namespace hcl;

int  CyclicBufferManager::s_ccbFillRoundForCurrStream   = -1;
bool CyclicBufferManager::s_ccbIsFullForDeviceBenchMark = false;

CyclicBufferManager::CyclicBufferManager(ScalStreamBase*       scalStream,
                                         ScalJsonNames&        scalNames,
                                         CompletionGroup&      cg,
                                         uint64_t              hostAddress,
                                         scal_stream_info_t&   streamInfo,
                                         uint64_t              bufferSize,
                                         std::string&          streamName,
                                         scal_stream_handle_t& streamHandle,
                                         Gen2ArchScalWrapper&  scalWrapper,
                                         unsigned              schedIdx,
                                         HclCommandsGen2Arch&  commands)
: m_scalStream(scalStream),
  m_scalNames(scalNames),
  m_cg(cg),
  m_pi(0),
  m_bufferSize(bufferSize),
  m_hostAddress(hostAddress),
  m_hostPi(0),
  m_streamInfo(streamInfo),
  m_divAlignment(bufferSize / m_numberOfDivisions),
  m_divIndex(m_numberOfDivisions - 1),
  m_targetValueOfBufferChunk(),
  m_streamName(streamName),
  m_streamHandle(streamHandle),
  m_scalWrapper(scalWrapper),
  m_schedIdx(schedIdx),
  m_commands(commands),
  m_logOfBufferSize((uint64_t)std::log2(m_bufferSize)),
  m_pi_mask((1 << m_logOfBufferSize) - 1)
{
    VERIFY((bufferSize & m_pi_mask) == 0, "bufferSize {} must be a power of two", bufferSize);

    for (unsigned i = 0; i < m_numberOfDivisions; ++i)
    {
        m_targetValueOfBufferChunk[i] = 0;
        m_targetValueOfBufferSet[i]   = true;
    }
}

void CyclicBufferManager::setTargetValue(uint64_t targetValue)
{
    m_targetValue = targetValue;
}

void CyclicBufferManager::advanceAlignment(size_t size)
{
    // These static consts are used to get the rightmost bits of the respective values, without using modulo.
    // See getPi() for more information on the formulas.
    static const uint64_t commandAlignmentShift = std::log2(m_streamInfo.command_alignment);
    static const uint64_t commandAlignmentMask  = ((1 << commandAlignmentShift) - 1);
    static const uint64_t divAlignmentShift     = std::log2(m_divAlignment);
    static const uint64_t divAlignmentMask      = ((1 << divAlignmentShift) - 1);

    uint64_t startAddress = getPi();
    uint64_t endAddress   = startAddress + size - 1;

    if ((startAddress >> commandAlignmentShift) < (endAddress >> commandAlignmentShift))
    {
        uint64_t alignSize = m_streamInfo.command_alignment - (startAddress & commandAlignmentMask);

        m_commands.serializeNopCommand(*m_scalStream, m_schedIdx, alignSize);
        incPi(alignSize);

        startAddress = getPi();

        LOG_HCL_TRACE(HCL_SCAL,
                      "On microStream {} Added alignment of size {}, after alignment new counter-pi: {}, new-pi {}",
                      m_streamName,
                      alignSize,
                      m_pi,
                      getPi());
    }

    if ((startAddress & divAlignmentMask) == 0)
    {
        submit(/*force=*/true);

        LOG_HCL_TRACE(HCL_SCAL,
                      "On microStream {} send NOP for moving to next division, pi = {}, on jobNr = {}. stream "
                      "handle: 0x{:x}",
                      m_streamName,
                      getPi(),
                      m_targetValue,
                      (uint64_t)m_streamHandle);

        moveToNextDivision();
    }

    m_targetValueOfBufferChunk[m_divIndex] = m_targetValue;

    uint64_t prevDivIndex = m_divIndex - 1;
    if (m_divIndex == 0) prevDivIndex = m_numberOfDivisions - 1;

    // workaround to ci/pi updating FW delay
    if (((getPi() - m_divAlignment * m_divIndex) >= 512) && m_targetValueOfBufferSet[prevDivIndex])
    {
        m_targetValueOfBufferChunk[prevDivIndex] = m_targetValue;
        m_targetValueOfBufferSet[prevDivIndex]   = false;
    }

    m_sizeLeftInAlignment = (1 << commandAlignmentShift);
}

void* CyclicBufferManager::getNextPtr(size_t size)
{
    constexpr int  dummyBuffSize = 256;  // big enough for any packet
    static uint8_t dummyBuff[dummyBuffSize];

    if (m_disableCcb)
    {
        assert(dummyBuffSize >= size);
        return dummyBuff;
    }

    if (unlikely(size > m_sizeLeftInAlignment))
    {
        incPi(m_sizeSinceAlignment);
        advanceAlignment(size);
    }

    void* ptr = reinterpret_cast<void*>(m_hostAddress + m_hostPi);
    m_hostPi += size;
    m_sizeSinceAlignment += size;
    m_sizeLeftInAlignment -= size;
    return ptr;

#if 0  // only enable for debugging!
    LOG_HCL_TRACE(HCL_SCAL,
                  "On microStream {} send command, pi = {}, on jobNr = {}. stream "
                  "handle: 0x{:x}",
                  m_streamName,
                  getPi(),
                  targetValue,
                  (uint64_t)m_streamHandle);

    uint32_t           opcode      = *((uint32_t*)((uint32_t*)(m_hostAddress + startAddress))) & 0x1F;
    const std::string& commandName = m_scalNames.getCommandName(opcode, m_schedIdx);

    try
    {
        LOG_HCL_TRACE(
            HCL_SCAL,
            "On microStream {} Added command 0x{:x} with size {}, opcode: {} and cg target {}, new counter-pi: {}, "
            "new-pi {}, cyclic buffer host address: 0x{:x}",
            m_streamName,
            (uint64_t)addr,
            size,
            commandName,
            targetValue,
            m_counter,
            getPi(),
            (uint64_t)m_hostAddress + startAddress);

        if (commandName.find("LBW_WRITE") != std::string::npos)
        {
            const sched_arc_cmd_lbw_write_t& pkt = reinterpret_cast<const sched_arc_cmd_lbw_write_t&>(
                *((uint32_t*)((uint32_t*)(m_hostAddress + startAddress))));

            LOG_HCL_TRACE(HCL_SCAL,
                          "On microStream {} de-ser --> opcode: {} src_data: 0x{:x}, dst_addr: 0x{:x}, block_next: {}",
                          m_streamName,
                          commandName,
                          (uint64_t)pkt.src_data,
                          (uint64_t)pkt.dst_addr,
                          pkt.block_next);
        }

        if (commandName.find("FENCE_WAIT") != std::string::npos)
        {
            const sched_arc_cmd_fence_wait_t& pkt = reinterpret_cast<const sched_arc_cmd_fence_wait_t&>(
                *((uint32_t*)((uint32_t*)(m_hostAddress + startAddress))));

            LOG_HCL_TRACE(HCL_SCAL,
                          "On microStream {} de-ser --> opcode: {} fence-id: {}, target: {}",
                          m_streamName,
                          commandName,
                          pkt.fence_id,
                          pkt.target);
        }
    }
    catch (std::out_of_range& e)
    {
        LOG_HCL_WARN(HCL_SCAL,
                     "Used wrong opcode number? On microStream {} Added command 0x{:x} with size {}, "
                     "opcode idx:{} and cg target {}, new "
                     "counter-pi: {}, "
                     "new-pi {}, cyclic buffer host address: 0x{:x}",
                     m_streamName,
                     (uint64_t)addr,
                     size,
                     opcode,
                     targetValue,
                     m_counter,
                     getPi(),
                     (uint64_t)m_hostAddress + startAddress);
    }
#endif
}

void CyclicBufferManager::submit(bool force)
{
    if (force || requiresSubmission())
    {
        incPi(m_sizeSinceAlignment);
        m_scalWrapper.sendStream(m_streamHandle, getPi(), m_streamInfo.submission_alignment);
    }
}

bool CyclicBufferManager::isACcbHalfFullForDeviceBenchMark()
{
    const bool ret                                     = CyclicBufferManager::s_ccbIsFullForDeviceBenchMark;
    CyclicBufferManager::s_ccbIsFullForDeviceBenchMark = false;
    return ret;
}

/*For the device bench mark in HPT
We would like to avoid back pressure from exceeding the ccb size.
So this code raises a flag for the hpt when we have filled half of the ccb.
ccbFillRoundForCurrStream, indicates how many times we have filled half of the ccb for a specific stream.
We assume that there is 1 stream that gets filled faster than the rest.
This stream will always have a higher or equal round as the others.
Using the round mechanism we allow only this stream to change the flag since it will always fill up first.
*/
void CyclicBufferManager::updateCcbHalfFullMechanism()
{
    // check if we are writing to the middle or last division
    if (m_divIndex == (m_numberOfDivisions - 1) || m_divIndex == ((m_numberOfDivisions >> 1) - 1))
    {
        // calculate this streams round
        const int ccbFillRoundForCurrStream = (int)(m_pi >> (m_logOfBufferSize - 1));

        // if the round is greater than the last round that raised the flag
        if (CyclicBufferManager::s_ccbFillRoundForCurrStream < ccbFillRoundForCurrStream)
        {
            // update the round and raise the flag
            CyclicBufferManager::s_ccbFillRoundForCurrStream   = ccbFillRoundForCurrStream;
            CyclicBufferManager::s_ccbIsFullForDeviceBenchMark = true;
        }
    }
}

void CyclicBufferManager::moveToNextDivision()
{
    if (++m_divIndex >= m_numberOfDivisions) m_divIndex = 0;

    uint64_t prevDivIndex = m_divIndex - 1;
    if (m_divIndex == 0) prevDivIndex = m_numberOfDivisions - 1;

    if (unlikely(GCFG_NOTIFY_ON_CCB_HALF_FULL_FOR_DBM.value()))
    {
        updateCcbHalfFullMechanism();
    }

    m_cg.waitOnValue(m_targetValueOfBufferChunk[m_divIndex]);  // this call blocking on host
    m_targetValueOfBufferChunk[m_divIndex] = 0;
    m_targetValueOfBufferSet[prevDivIndex] = true;

    LOG_HCL_TRACE(HCL_SCAL,
                  "On microStream {} Moved to next division {} division value {}",
                  m_streamName,
                  m_divIndex,
                  m_targetValueOfBufferChunk[m_divIndex]);
}

void CyclicBufferManager::dfaLog(hl_logger::LoggerSPtr synDevFailLog)
{
    scal_buffer_info_t scalBuffInfo;
    int                rc = scal_buffer_get_info(m_streamInfo.control_core_buffer, &scalBuffInfo);
    if (rc != SCAL_SUCCESS)
    {
        HLLOG_UNTYPED(synDevFailLog, HLLOG_LEVEL_ERROR, "Could not get scalBuffInfo rc {}", rc);
    }

    unsigned schedulerIdx = 0;

    scal_control_core_info_t streamCoreInfo {};
    rc = scal_control_core_get_info(m_streamInfo.scheduler_handle, &streamCoreInfo);
    if (rc != SCAL_SUCCESS)
    {
        HLLOG_UNTYPED(synDevFailLog, HLLOG_LEVEL_ERROR, "Could not get streamCoreInfo rc {}", rc);
    }

    schedulerIdx              = streamCoreInfo.idx;
    std::string schedulerName = streamCoreInfo.name ? streamCoreInfo.name : "N/A";

    const std::string dfaInfo = fmt::format("PI: 0x{:x}", m_pi);

    const uint64_t ccbSize = m_bufferSize;
    std::string    out =
        fmt::format("\n#ccb stream-name {} scheduler-name {} scheduler-idx {} stream-idx {} CCB info: {:x} size {:x}\n",
                    m_streamName,
                    schedulerName,
                    schedulerIdx,
                    m_streamInfo.index,
                    m_pi,
                    ccbSize);

    out += fmt::format("#registers {:x} {:x}\n", scalBuffInfo.device_address, ccbSize);

    for (unsigned i = 0; i < ccbSize / sizeof(uint32_t); i += REGS_PER_LINE)
    {
        out += fmt::format("{:016x}: ", i * sizeof(uint32_t) + scalBuffInfo.device_address);

        for (int j = 0; j < REGS_PER_LINE; j++)
        {
            out += fmt::format("{:08x} ", reinterpret_cast<uint32_t*>(m_hostAddress)[i + j]);
        }

        if (i != (ccbSize - REGS_PER_LINE))
        {
            out += "\n";
        }
    }

    HLLOG_UNTYPED(synDevFailLog, HLLOG_LEVEL_INFO, "{}", out);
}