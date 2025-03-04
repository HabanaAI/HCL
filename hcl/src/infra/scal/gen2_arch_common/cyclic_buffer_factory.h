#pragma once

#include <cstdint>                                              // for uint64_t, uint32_t
#include "infra/scal/gen2_arch_common/cyclic_buffer_manager.h"  // for CyclicBufferManager
#include "factory_types.h"                                      // for CyclicBufferType

namespace hcl
{

/**
 * @brief
 *
 * CyclicBufferFactory is responsible for creating a CyclicBufferManager
 */
class CyclicBufferFactory
{
public:
    static std::unique_ptr<CyclicBufferManager> createCyclicBuffer(CyclicBufferType      type,
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
                                                                   HclCommandsGen2Arch&  commands);
};
}  // namespace hcl