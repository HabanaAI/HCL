#include "infra/scal/gen2_arch_common/cyclic_buffer_factory.h"

#include <cstdint>                                    // for uint64_t, uint32_t
#include "hcl_utils.h"                                // for VERIFY
#include "infra/scal/gaudi_common/factory_types.h"    // for CyclicBufferType
#include "infra/scal/gaudi2/cyclic_buffer_manager.h"  // for Gaudi2CyclicBufferManager
#include "infra/scal/gaudi3/cyclic_buffer_manager.h"  // for Gaudi3CyclicBufferManager

using namespace hcl;

std::unique_ptr<CyclicBufferManager> CyclicBufferFactory::createCyclicBuffer(CyclicBufferType      type,
                                                                             ScalStreamBase*       scalStream,
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
{
    switch (type)
    {
        case CyclicBufferType::GAUDI2:
            return std::make_unique<Gaudi2CyclicBufferManager>(scalStream,
                                                               scalNames,
                                                               cg,
                                                               hostAddress,
                                                               streamInfo,
                                                               bufferSize,
                                                               streamName,
                                                               streamHandle,
                                                               scalWrapper,
                                                               schedIdx,
                                                               commands);
        case CyclicBufferType::GAUDI3:
            return std::make_unique<Gaudi3CyclicBufferManager>(scalStream,
                                                               scalNames,
                                                               cg,
                                                               hostAddress,
                                                               streamInfo,
                                                               bufferSize,
                                                               streamName,
                                                               streamHandle,
                                                               scalWrapper,
                                                               schedIdx,
                                                               commands);
        default:
            VERIFY(false,
                   "Provided unsupported CyclicBufferType={}. CyclicBufferType can be only of type "
                   "CyclicBufferType::GAUDI2 or CyclicBufferType::GAUDI3",
                   type);
    }
};
