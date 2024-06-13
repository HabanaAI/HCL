#include "scal_stream.h"

#include <bits/exception.h>         // for exception
#include <stdexcept>                // for runtime_error
#include "cyclic_buffer_manager.h"  // for CyclicBufferManager
#include "hcl_utils.h"              // for LOG_HCL_TRACE, UNUSED, VERIFY
#include "hcl_log_manager.h"        // for LOG*
#include "scal_wrapper.h"           // for Gen2ArchScalWrapper

using namespace hcl;

void* ScalStreamBase::getNextPtr(size_t size)
{
    VERIFY(size % sizeof(uint32_t) == 0);
    m_buffer.resize(size / sizeof(uint32_t));
    return m_buffer.data();
}

ScalStream::ScalStream(ScalJsonNames&       scalNames,
                       const std::string&   name,
                       Gen2ArchScalWrapper& scalWrapper,
                       CompletionGroup&     cg,
                       unsigned             schedIdx,
                       unsigned             internalStreamIdx,
                       unsigned             archStreamIndex,
                       HclCommandsGen2Arch& commands)
: m_scalNames(scalNames),
  m_scalWrapper(scalWrapper),
  m_streamName(name),
  m_schedIdx(schedIdx),
  m_internalStreamIdx(internalStreamIdx),
  m_archStreamIndex(archStreamIndex)
{
    m_scalWrapper.initStream(name, m_streamHandle, m_streamInfo, m_hostCyclicBufferSize, m_bufferHandle, m_bufferInfo);

    LOG_HCL_TRACE(HCL_SCAL,
                  "Created new Stream {} with handle 0x{:x}, and buffer handle 0x{:x}, on host address: 0x{:x}",
                  m_streamName,
                  (uint64_t)m_streamHandle,
                  (uint64_t)m_bufferHandle,
                  (uint64_t)m_bufferInfo.host_address);
}

void ScalStream::setTargetValue(uint64_t targetValue)
{
    m_cyclicBuffer->setTargetValue(targetValue);
}

void* ScalStream::getNextPtr(size_t size)
{
    return m_cyclicBuffer->getNextPtr(size);
}

bool ScalStream::requiresSubmission()
{
    return m_cyclicBuffer->requiresSubmission();
}

void ScalStream::submit()
{
    m_cyclicBuffer->submit();
}

bool ScalStream::isACcbHalfFullForDeviceBenchMark()
{
    return CyclicBufferManager::isACcbHalfFullForDeviceBenchMark();
}

void ScalStream::disableCcb(bool disable)
{
    m_cyclicBuffer->disableCcb(disable);
}

void ScalStream::dfaLog(hl_logger::LoggerSPtr synDevFailLog)
{
    m_cyclicBuffer->dfaLog(synDevFailLog);
}

ScalStream::~ScalStream()
{
    try
    {
        m_scalWrapper.freeBuffer(m_bufferHandle);
    }
    catch (const std::runtime_error& re)
    {
        LOG_WARN(HCL_SCAL, "Runtime error: {}", re.what());
    }
    catch (const std::exception& ex)
    {
        LOG_WARN(HCL_SCAL, "Error occurred: {}", ex.what());
    }
    catch (...)
    {
        LOG_WARN(HCL_SCAL, "Unknown failure occurred. Possible memory corruption");
    }
}
