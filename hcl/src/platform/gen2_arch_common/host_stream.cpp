#include "platform/gen2_arch_common/host_stream.h"
#include "hcl_utils.h"  // for LOG_HCL_INFO, LOG_H...

HostStream::HostStream(const std::string& name,
                       unsigned           archStreamIdx,
                       unsigned           uarchStreamIdx,
                       spHostStreamFifo   innerQueue,
                       HostStreamType     type)
: m_streamName(name),
  m_innerQueue(innerQueue),
  m_archStreamIdx(archStreamIdx),
  m_uarchStreamIdx(uarchStreamIdx),
  m_type(type)
{
    LOG_HCL_INFO(HCL, "Create {}", m_streamName);

    m_outerQueue        = std::make_shared<HostStreamFifo>(m_streamName);
    m_timerStarted      = false;
    m_ongoingProcessing = false;
    m_startTime         = std::chrono::steady_clock::now();
    m_endTime           = std::chrono::steady_clock::now();
}

bool HostStream::isEmpty()
{
    return m_innerQueue->isEmpty() && m_outerQueue->isEmpty();
}
