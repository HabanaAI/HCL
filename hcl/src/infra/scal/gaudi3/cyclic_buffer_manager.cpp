#include "infra/scal/gaudi3/cyclic_buffer_manager.h"
#include <cmath>

hcl::Gaudi3CyclicBufferManager::Gaudi3CyclicBufferManager(ScalStreamBase*       scalStream,
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
: CyclicBufferManager(scalStream,
                      scalNames,
                      cg,
                      hostAddress,
                      streamInfo,
                      bufferSize,
                      streamName,
                      streamHandle,
                      scalWrapper,
                      schedIdx,
                      commands)
{
}

uint64_t hcl::Gaudi3CyclicBufferManager::getPi()
{
    return m_pi;
}

void hcl::Gaudi3CyclicBufferManager::incPi(uint32_t size)
{
    m_pi += size;
    static const uint64_t shift = std::log2(m_bufferSize);
    m_hostPi                    = m_pi & ((1 << shift) - 1);
    m_sizeSinceAlignment        = 0;
}