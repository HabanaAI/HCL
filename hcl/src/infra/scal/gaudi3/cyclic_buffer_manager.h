#pragma once

#include "infra/scal/gen2_arch_common/cyclic_buffer_manager.h"

namespace hcl
{
/**
 * @brief
 *
 * CyclicBufferManager is responsible for managing cyclic buffer AKA MicroArchStream.
 * It responsible on adding commands to the buffer, managing the pi and alignment.
 * ** FOr now, it not responsible for sending the buffer to the device.
 *
 */
class Gaudi3CyclicBufferManager : public CyclicBufferManager
{
public:
    Gaudi3CyclicBufferManager(ScalStreamBase*       scalStream,
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
    virtual uint64_t getPi() override;

protected:
    virtual void incPi(uint32_t size) override;
};
}  // namespace hcl
