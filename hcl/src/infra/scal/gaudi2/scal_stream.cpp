#include "infra/scal/gaudi2/scal_stream.h"
#include "infra/scal/gaudi2/cyclic_buffer_manager.h"
#include "hcl_log_manager.h"

hcl::Gaudi2ScalStream::Gaudi2ScalStream(ScalJsonNames&       scalNames,
                                        const std::string&   name,
                                        Gen2ArchScalWrapper& scalWrapper,
                                        CompletionGroup&     cg,
                                        unsigned             schedIdx,
                                        unsigned             internalStreamIdx,
                                        unsigned             archStreamIndex,
                                        HclCommandsGen2Arch& commands)
: ScalStream(scalNames, name, scalWrapper, cg, schedIdx, internalStreamIdx, archStreamIndex, commands)
{
    m_cyclicBuffer =
        std::unique_ptr<CyclicBufferManager>(new Gaudi2CyclicBufferManager(this,
                                                                           scalNames,
                                                                           cg,
                                                                           (uint64_t)m_bufferInfo.host_address,
                                                                           m_streamInfo,
                                                                           m_hostCyclicBufferSize,
                                                                           m_streamName,
                                                                           m_streamHandle,
                                                                           scalWrapper,
                                                                           m_schedIdx,
                                                                           commands));
}