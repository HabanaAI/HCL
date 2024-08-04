#pragma once

#include <string>
#include <map>
#include "infra/hcl_affinity_manager.h"  // for HclThread
#include "hcl_utils.h"

class HostStream;
class HclDeviceGen2Arch;

enum sched_host_opcode
{
    HOST_SCHED_CMD_NULL = 0,
    HOST_SCHED_CMD_FENCE_WAIT,
    HOST_SCHED_CMD_SEND,
    HOST_SCHED_CMD_RECV,
    HOST_SCHED_CMD_SEND_WITH_FENCE,
    HOST_SCHED_CMD_RECV_WITH_FENCE,
    HOST_SCHED_CMD_WAIT_FOR_COMP,
    HOST_SCHED_CMD_SIGNAL_SO,
    HOST_SCHED_CMD_NUM
};

class HostSchedCommandNames
{
public:
    HostSchedCommandNames();
    const std::string&              getCommandName(uint32_t opcode);
    std::map<uint32_t, std::string> hostSchedCmdsNames;

};  // class HostSchedCommandNames

// clang-format off
inline HostSchedCommandNames::HostSchedCommandNames()
{
    map_init(hostSchedCmdsNames)
        (HOST_SCHED_CMD_FENCE_WAIT, "fenceWait")
        (HOST_SCHED_CMD_SEND, "SendCmd")
        (HOST_SCHED_CMD_RECV, "RecvCmd")
        (HOST_SCHED_CMD_SEND_WITH_FENCE, "SendWithFenceWait")
        (HOST_SCHED_CMD_RECV_WITH_FENCE, "RecvWithFenceWait")
        (HOST_SCHED_CMD_WAIT_FOR_COMP, "WaitForComp")
        (HOST_SCHED_CMD_SIGNAL_SO, "SignalSo")
    ;
}
// clang-format on

inline const std::string& HostSchedCommandNames::getCommandName(uint32_t opcode)
{
    static std::string invalid = "<invalid>";
    if (hostSchedCmdsNames.count(opcode) == 0)
    {
        LOG_WARN(HCL, "Invalid opcode {} in host sched command", opcode);
        return invalid;
    }

    return hostSchedCmdsNames.at(opcode);
}

// opcode below is 4 bits
static_assert(HOST_SCHED_CMD_NUM <= 1 << 4, "Maximum 16 host scheduler commands are supported");

struct OfiCompCallbackParams;
// Since we use container_of to fetch ofi_req_t when processing CQEs, the callback can't be std::function type
typedef void (*CompCallBack)(OfiCompCallbackParams*);

struct OfiCompCallbackParams
{
    unsigned           smIdx;
    unsigned           soIdx;
    uint32_t           value;
    HclDeviceGen2Arch* device;
    CompCallBack       compCallBack = nullptr;
} __attribute__((aligned(4), __packed__));

struct host_sched_cmd_scale_out_nic_op
{
    uint32_t opcode : 4;
    uint16_t qpSetIndex : 4;
    uint32_t __unused : 8;
    uint32_t rank : 16; // HCL_Rank
    static_assert(sizeof(HCL_Rank) == sizeof(uint16_t), "Rank size must be 16 bits");
    uint64_t address;
    uint64_t size;
    HCL_Comm comm;  // uint32_t
    uint64_t srCount;  // for debug
    OfiCompCallbackParams compParams;
} __attribute__((aligned(4), __packed__));

struct host_sched_cmd_scale_out_with_fence_nic_op
{
    uint32_t opcode : 4;
    uint32_t qpSetIndex : 4;
    uint32_t askForCredit : 1;
    uint32_t __unused : 7;
    uint32_t rank : 16;  // HCL_Rank
    static_assert(sizeof(HCL_Rank) == sizeof(uint16_t), "Rank size must be 16 bits");
    uint64_t address;
    uint64_t size;
    HCL_Comm comm;  // uint32_t
    unsigned fenceIdx;
    uint64_t srCount;  // for debug
    OfiCompCallbackParams compParams;
} __attribute__((aligned(4), __packed__));

struct host_sched_cmd_wait_for_completion
{
    uint32_t opcode : 4;
    uint32_t isSend : 1;  // for debug
    uint32_t reserved : 27;
    HCL_Comm comm;
    uint64_t srCount;  // for debug
} __attribute__((aligned(4), __packed__));

struct host_sched_cmd_fence_wait
{
    uint32_t opcode : 4;
    uint32_t askForCredit : 1;
    uint32_t reserved : 27;
    unsigned fenceIdx;
    uint64_t srCount;  // for debug
} __attribute__((aligned(4), __packed__));

struct host_sched_cmd_signal_so
{
    uint32_t opcode : 4;
    uint32_t reserved : 28;
    OfiCompCallbackParams compParams;
} __attribute__((aligned(4), __packed__));

class HostScheduler
{
public:
    HostScheduler() = default;
    virtual ~HostScheduler();

    HostScheduler(HostScheduler&)  = delete;
    HostScheduler(HostScheduler&&) = delete;
    HostScheduler&  operator=(HostScheduler&) = delete;
    HostScheduler&& operator=(HostScheduler&&) = delete;

    void runHostScheduler();

    void startThread(HclDeviceGen2Arch* device, unsigned index, std::vector<HostStream*>& hostStreams);
    void notifyThread();
    void stopThread();

private:
    std::vector<HostStream*> m_hostStreams;
    uint32_t*                m_hostStreamCmd = nullptr;
    HostSchedCommandNames    m_cmdNames;

    HclThread          m_thread;
    volatile bool      m_stop   = true;
    HclDeviceGen2Arch* m_device = nullptr;
    unsigned                m_index;
    std::mutex              m_submittedWorkMutex;
    volatile bool           m_submittedWork = false;
    std::condition_variable m_submittedWorkCondVar;
    uint64_t                m_sleepThreshold;
    std::chrono::milliseconds m_sleepDuration;

    void processStream(HostStream* hostStream);
    bool processScaleOutCommand(HostStream* hostStream);
    bool processScaleOutWithFenceCommand(HostStream* hostStream);
    bool processScaleoutWaitForCompCommand(HostStream* hostStream, uint64_t& srCount, uint64_t& submitTime);
    bool processFenceWaitCommand(HostStream* hostStream);
    bool processSignalSoCommand(HostStream* hostStream);
    uint32_t getStreamDepthProc(HostStream* hostStream);
};