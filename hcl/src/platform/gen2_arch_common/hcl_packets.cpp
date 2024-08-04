#include "platform/gen2_arch_common/hcl_packets.h"

#include "platform/gen2_arch_common/host_scheduler.h"  // for host_sched_cmd_scale_out_nic_op

void HostSchedCommandsGen2Arch::serializeHostSendScaleOutCommand(spHostStreamFifo       hostStream,
                                                                 bool                   isSend,
                                                                 uint64_t               address,
                                                                 HCL_Rank               rank,
                                                                 uint64_t               size,
                                                                 HCL_Comm               comm,
                                                                 OfiCompCallbackParams& compParams,
                                                                 const uint64_t         srCount,
                                                                 uint16_t               qpSetIndex)
{
    LOG_DEBUG(HCL_SUBMIT,
              "HostSchedCommandsGen2Arch::serializeHostSendScaleOutCommand: isSend={}, address=0x{:x}, rank={}, "
              "size={}, comm={}, srCount={}, qpSetIndex={}",
              isSend,
              address,
              rank,
              size,
              comm,
              srCount,
              qpSetIndex);
    static size_t                    dwords = sizeof(host_sched_cmd_scale_out_nic_op) >> 2;
    host_sched_cmd_scale_out_nic_op* command =
        reinterpret_cast<host_sched_cmd_scale_out_nic_op*>(hostStream->getNextPtr(dwords));
    command->opcode     = isSend ? HOST_SCHED_CMD_SEND : HOST_SCHED_CMD_RECV;
    command->qpSetIndex = qpSetIndex;
    command->address    = address;
    command->rank       = rank;
    command->size       = size;
    command->comm       = comm;
    command->compParams = compParams;
    command->srCount    = srCount;

    hostStream->submit();
}

void HostSchedCommandsGen2Arch::serializeHostScaleOutCommandWithFence(spHostStreamFifo       hostStream,
                                                                      bool                   isSend,
                                                                      uint64_t               address,
                                                                      HCL_Rank               rank,
                                                                      uint64_t               size,
                                                                      HCL_Comm               comm,
                                                                      unsigned               fenceIdx,
                                                                      OfiCompCallbackParams& compParams,
                                                                      const uint64_t         srCount,
                                                                      uint16_t               qpSetIndex)
{
    LOG_DEBUG(HCL_SUBMIT,
              "HostSchedCommandsGen2Arch::serializeHostScaleOutCommandWithFence: isSend={}, address=0x{:x}, rank={}, "
              "size={}, comm={}, fenceIdx={}, srCount={}, qpSetIndex={}",
              isSend,
              address,
              rank,
              size,
              comm,
              fenceIdx,
              srCount,
              qpSetIndex);
    static size_t                               dwords = sizeof(host_sched_cmd_scale_out_with_fence_nic_op) >> 2;
    host_sched_cmd_scale_out_with_fence_nic_op* command =
        reinterpret_cast<host_sched_cmd_scale_out_with_fence_nic_op*>(hostStream->getNextPtr(dwords));
    command->opcode       = isSend ? HOST_SCHED_CMD_SEND_WITH_FENCE : HOST_SCHED_CMD_RECV_WITH_FENCE;
    command->qpSetIndex   = qpSetIndex;
    command->address      = address;
    command->rank         = rank;
    command->size         = size;
    command->comm         = comm;
    command->fenceIdx     = fenceIdx;
    command->askForCredit = 1;
    command->compParams   = compParams;
    command->srCount      = srCount;

    hostStream->submit();
}

void HostSchedCommandsGen2Arch::serializeHostWaitForCompletionCommand(spHostStreamFifo hostStream,
                                                                      HCL_Comm         comm,
                                                                      const uint64_t   srCount)
{
    static size_t                       dwords = sizeof(host_sched_cmd_wait_for_completion) >> 2;
    host_sched_cmd_wait_for_completion* command =
        reinterpret_cast<host_sched_cmd_wait_for_completion*>(hostStream->getNextPtr(dwords));
    command->opcode  = HOST_SCHED_CMD_WAIT_FOR_COMP;
    command->comm    = comm;
    command->srCount = srCount;
    hostStream->submit();
}

void HostSchedCommandsGen2Arch::serializeHostFenceCommand(spHostStreamFifo hostStream,
                                                          unsigned         fenceIdx,
                                                          const uint64_t   srCount)
{
    static size_t              dwords  = sizeof(host_sched_cmd_fence_wait) >> 2;
    host_sched_cmd_fence_wait* command = reinterpret_cast<host_sched_cmd_fence_wait*>(hostStream->getNextPtr(dwords));
    command->opcode                    = HOST_SCHED_CMD_FENCE_WAIT;
    command->fenceIdx                  = fenceIdx;
    command->askForCredit              = 1;
    command->srCount                   = srCount;
    hostStream->submit();
}

void HostSchedCommandsGen2Arch::serializeHostSignalSoCommand(spHostStreamFifo hostStream,
                                                             unsigned         smIdx,
                                                             unsigned         soIdx,
                                                             uint32_t         value)
{
    static size_t             dwords  = sizeof(host_sched_cmd_signal_so) >> 2;
    host_sched_cmd_signal_so* command = reinterpret_cast<host_sched_cmd_signal_so*>(hostStream->getNextPtr(dwords));
    command->opcode                   = HOST_SCHED_CMD_SIGNAL_SO;
    command->compParams.smIdx         = smIdx;
    command->compParams.soIdx         = soIdx;
    command->compParams.value         = value;
    hostStream->submit();
}