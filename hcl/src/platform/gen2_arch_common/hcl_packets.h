#pragma once

#include "platform/gen2_arch_common/host_stream.h"  // for spHostStreamFifo
#include "platform/gen2_arch_common/host_scheduler.h"  // for OfiCompCallbackParams

namespace HostSchedCommandsGen2Arch
{
void serializeHostSendScaleOutCommand(spHostStreamFifo       hostStream,
                                      bool                   isSend,
                                      uint64_t               address,
                                      HCL_Rank               rank,
                                      uint64_t               size,
                                      HCL_Comm               comm,
                                      OfiCompCallbackParams& compParams,
                                      const uint64_t         srCount,
                                      uint16_t               qpSetIndex);

void serializeHostScaleOutCommandWithFence(spHostStreamFifo       hostStream,
                                           bool                   isSend,
                                           uint64_t               address,
                                           HCL_Rank               rank,
                                           uint64_t               size,
                                           HCL_Comm               comm,
                                           unsigned               fenceIdx,
                                           OfiCompCallbackParams& compParams,
                                           const uint64_t         srCount,
                                           uint16_t               qpSetIndex);

void serializeHostWaitForCompletionCommand(spHostStreamFifo hostStream, HCL_Comm comm, const uint64_t srCount);

void serializeHostFenceCommand(spHostStreamFifo hostStream, unsigned fenceIdx, const uint64_t srCount);

void serializeHostSignalSoCommand(spHostStreamFifo hostStream, unsigned soDcore, unsigned soIdx, uint32_t value);

}  // namespace HostSchedCommandsGen2Arch
