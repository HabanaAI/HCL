#pragma once

#include <vector>
#include <string>
#include <map>
#include <array>
#include "hcl_utils.h"
#include "sched_pkts.h"

namespace hcl
{
enum class SchedulersIndex
{
    dma = 0,       // "network_garbage_collector_and_reduction"
    sendScaleUp,   // "scaleup_send"
    recvScaleUp,   // "scaleup_receive"
    sendScaleOut,  // "scaleout_send"
    recvScaleOut,  // "scaleout_receive"
    count,
};

enum class DMAStreams
{
    garbageCollection = 0,
    reduction         = 1,
    arbitrator        = 2,
    scaleoutReduction = 3,
    signaling         = 4,
    gdr               = 5,
    max               = 6
};

enum class SyncManagerName
{
    networkMonitor = 0,  // "network_gp_monitors_"
    longMonitor,         // "network_long_monitors_"
    so,                  // "network_gp_sos_"
    cgInternal,          // "network_completion_queue_internal_"
    cgExternal,          // "network_completion_queue_external_"
    count,
};

enum class SchedulerType
{
    internal = 0,
    external,
    count,
};

class ScalJsonNames
{
public:
    static constexpr int numberOfArchsStreams                  = 3;
    static constexpr int numberOfMicroArchsStreamsPerScheduler = 32;

    /**
     * @brief Construct a new Scal Json Names object
     *
     * ScalJsonNames is naming binded to the scal json comfiguration names.
       It hold all naming and maping and order for scal HCL SW layer needs.
     */

    ScalJsonNames();

    const std::string& getCommandName(uint32_t opcode, uint32_t schedIdx);
    const std::string  getFenceName(unsigned archStreamIdx, unsigned fenceIdx);

    std::map<SchedulersIndex, std::string>                                   schedulersNames;
    std::array<std::map<SyncManagerName, std::string>, numberOfArchsStreams> smNames;

    std::vector<unsigned> numberOfMicroArchStreams = {
        6,  // dma scheduler, streams: 0:cleanup, 1:reduction-scaleup, 2:arb, 3:reduction-scaleout, 4:signaling-stream,
            // 5:gaudi-direct
        3,  // scaleup send scheduler, streams: 0:RS, 1:AG, 2:arb
        3,  // scaleup recv scheduler, streams: 0:RS, 1:AG, 2:arb
        3,  // scaleout send scheduler, streams: 0:RS, 1:AG, 2:arb
        3   // scaleout recv scheduler, streams: 0:RS, 1:AG, 2:arb
    };

    std::map<uint32_t, std::string> scaleupSendCmdName;
    std::map<uint32_t, std::string> scaleupRecvCmdName;
    std::map<uint32_t, std::string> scaleoutSendCmdName;
    std::map<uint32_t, std::string> scaleoutRecvCmdName;
    std::map<uint32_t, std::string> dmaCmdName;
    const std::string               hostFenceNamePrefix = "host_fence_counters_";
};

// clang-format off
inline ScalJsonNames::ScalJsonNames()
{
    map_init(scaleupSendCmdName)
	(g2fw::SCHED_SCALEUP_SEND_ARC_CMD_FENCE_WAIT,           "SCHED_SCALEUP_SEND_ARC_CMD_FENCE_WAIT")
	(g2fw::SCHED_SCALEUP_SEND_ARC_CMD_LBW_WRITE,            "SCHED_SCALEUP_SEND_ARC_CMD_LBW_WRITE")
	(g2fw::SCHED_SCALEUP_SEND_ARC_CMD_LBW_BURST_WRITE,      "SCHED_SCALEUP_SEND_ARC_CMD_LBW_BURST_WRITE")
	(g2fw::SCHED_SCALEUP_SEND_ARC_CMD_FENCE_INC_IMMEDIATE,  "SCHED_SCALEUP_SEND_ARC_CMD_FENCE_INC_IMMEDIATE")
	(g2fw::SCHED_SCALEUP_SEND_ARC_CMD_LBW_READ,             "SCHED_SCALEUP_SEND_ARC_CMD_LBW_READ")
	(g2fw::SCHED_SCALEUP_SEND_ARC_CMD_MEM_FENCE,            "SCHED_SCALEUP_SEND_ARC_CMD_MEM_FENCE")
	(g2fw::SCHED_SCALEUP_SEND_ARC_CMD_NOP,                  "SCHED_SCALEUP_SEND_ARC_CMD_NOP")
	(g2fw::SCHED_SCALEUP_SEND_ARC_CMD_UPDATE_NIC_GLBL_CTXT, "SCHED_SCALEUP_SEND_ARC_CMD_UPDATE_NIC_GLBL_CTXT")
	(g2fw::SCHED_SCALEUP_SEND_ARC_CMD_UPDATE_NIC_COLL_CTXT, "SCHED_SCALEUP_SEND_ARC_CMD_UPDATE_NIC_COLL_CTXT")
	(g2fw::SCHED_SCALEUP_SEND_ARC_CMD_NIC_COLL_OPS,         "SCHED_SCALEUP_SEND_ARC_CMD_NIC_COLL_OPS")
	(g2fw::SCHED_SCALEUP_SEND_ARC_CMD_ALLOC_NIC_BARRIER,    "SCHED_SCALEUP_SEND_ARC_CMD_ALLOC_NIC_BARRIER")
	(g2fw::SCHED_SCALEUP_SEND_ARC_CMD_NIC_PASSTHROUGH,      "SCHED_SCALEUP_SEND_ARC_CMD_NIC_PASSTHROUGH")
	(g2fw::SCHED_SCALEUP_SEND_ARC_CMD_NIC_EDMA_OPS,         "SCHED_SCALEUP_SEND_ARC_CMD_NIC_EDMA_OPS")
    ;

    map_init(scaleupRecvCmdName)
	(g2fw::SCHED_SCALEUP_RECV_ARC_CMD_FENCE_WAIT,           "SCHED_SCALEUP_RECV_ARC_CMD_FENCE_WAIT")
	(g2fw::SCHED_SCALEUP_RECV_ARC_CMD_LBW_WRITE,            "SCHED_SCALEUP_RECV_ARC_CMD_LBW_WRITE")
	(g2fw::SCHED_SCALEUP_RECV_ARC_CMD_LBW_BURST_WRITE,      "SCHED_SCALEUP_RECV_ARC_CMD_LBW_BURST_WRITE")
	(g2fw::SCHED_SCALEUP_RECV_ARC_CMD_FENCE_INC_IMMEDIATE,  "SCHED_SCALEUP_RECV_ARC_CMD_FENCE_INC_IMMEDIATE")
	(g2fw::SCHED_SCALEUP_RECV_ARC_CMD_LBW_READ,             "SCHED_SCALEUP_RECV_ARC_CMD_LBW_READ")
	(g2fw::SCHED_SCALEUP_RECV_ARC_CMD_MEM_FENCE,            "SCHED_SCALEUP_RECV_ARC_CMD_MEM_FENCE")
	(g2fw::SCHED_SCALEUP_RECV_ARC_CMD_NOP,                  "SCHED_SCALEUP_RECV_ARC_CMD_NOP")
	(g2fw::SCHED_SCALEUP_RECV_ARC_CMD_UPDATE_NIC_GLBL_CTXT, "SCHED_SCALEUP_RECV_ARC_CMD_UPDATE_NIC_GLBL_CTXT")
	(g2fw::SCHED_SCALEUP_RECV_ARC_CMD_UPDATE_NIC_COLL_CTXT, "SCHED_SCALEUP_RECV_ARC_CMD_UPDATE_NIC_COLL_CTXT")
	(g2fw::SCHED_SCALEUP_RECV_ARC_CMD_NIC_COLL_OPS,         "SCHED_SCALEUP_RECV_ARC_CMD_NIC_COLL_OPS")
	(g2fw::SCHED_SCALEUP_RECV_ARC_CMD_ALLOC_NIC_BARRIER,    "SCHED_SCALEUP_RECV_ARC_CMD_ALLOC_NIC_BARRIER")
	(g2fw::SCHED_SCALEUP_RECV_ARC_CMD_NIC_PASSTHROUGH,      "SCHED_SCALEUP_RECV_ARC_CMD_NIC_PASSTHROUGH")
    ;

    map_init(scaleoutSendCmdName)
	(g2fw::SCHED_SCALEOUT_SEND_ARC_CMD_FENCE_WAIT,          "SCHED_SCALEOUT_SEND_ARC_CMD_FENCE_WAIT")
	(g2fw::SCHED_SCALEOUT_SEND_ARC_CMD_LBW_WRITE,           "SCHED_SCALEOUT_SEND_ARC_CMD_LBW_WRITE")
	(g2fw::SCHED_SCALEOUT_SEND_ARC_CMD_LBW_BURST_WRITE,     "SCHED_SCALEOUT_SEND_ARC_CMD_LBW_BURST_WRITE")
	(g2fw::SCHED_SCALEOUT_SEND_ARC_CMD_FENCE_INC_IMMEDIATE, "SCHED_SCALEOUT_SEND_ARC_CMD_FENCE_INC_IMMEDIATE")
	(g2fw::SCHED_SCALEOUT_SEND_ARC_CMD_NOP,                 "SCHED_SCALEOUT_SEND_ARC_CMD_NOP")
	(g2fw::SCHED_SCALEOUT_SEND_ARC_CMD_ALLOC_NIC_BARRIER,   "SCHED_SCALEOUT_SEND_ARC_CMD_ALLOC_NIC_BARRIER")
	(g2fw::SCHED_SCALEOUT_SEND_ARC_CMD_NIC_COLL_OPS,        "SCHED_SCALEOUT_SEND_ARC_CMD_NIC_COLL_OPS")
	(g2fw::SCHED_SCALEOUT_SEND_ARC_CMD_LBW_READ,            "SCHED_SCALEOUT_SEND_ARC_CMD_LBW_READ")
	(g2fw::SCHED_SCALEOUT_SEND_ARC_CMD_MEM_FENCE,           "SCHED_SCALEOUT_SEND_ARC_CMD_MEM_FENCE")
    ;

    map_init(scaleoutRecvCmdName)
	(g2fw::SCHED_SCALEOUT_RECV_ARC_CMD_FENCE_WAIT,          "SCHED_SCALEOUT_RECV_ARC_CMD_FENCE_WAIT")
	(g2fw::SCHED_SCALEOUT_RECV_ARC_CMD_LBW_WRITE,           "SCHED_SCALEOUT_RECV_ARC_CMD_LBW_WRITE")
	(g2fw::SCHED_SCALEOUT_RECV_ARC_CMD_LBW_BURST_WRITE,     "SCHED_SCALEOUT_RECV_ARC_CMD_LBW_BURST_WRITE")
	(g2fw::SCHED_SCALEOUT_RECV_ARC_CMD_FENCE_INC_IMMEDIATE, "SCHED_SCALEOUT_RECV_ARC_CMD_FENCE_INC_IMMEDIATE")
	(g2fw::SCHED_SCALEOUT_RECV_ARC_CMD_NOP,                 "SCHED_SCALEOUT_RECV_ARC_CMD_NOP")
	(g2fw::SCHED_SCALEOUT_RECV_ARC_CMD_ALLOC_NIC_BARRIER,   "SCHED_SCALEOUT_RECV_ARC_CMD_ALLOC_NIC_BARRIER")
	(g2fw::SCHED_SCALEOUT_RECV_ARC_CMD_NIC_COLL_OPS,        "SCHED_SCALEOUT_RECV_ARC_CMD_NIC_COLL_OPS")
	(g2fw::SCHED_SCALEOUT_RECV_ARC_CMD_LBW_READ,            "SCHED_SCALEOUT_RECV_ARC_CMD_LBW_READ")
	(g2fw::SCHED_SCALEOUT_RECV_ARC_CMD_MEM_FENCE,           "SCHED_SCALEOUT_RECV_ARC_CMD_MEM_FENCE")
    ;

    map_init(dmaCmdName)
	(g2fw::SCHED_GC_REDUCTION_ARC_CMD_FENCE_WAIT,          "SCHED_GC_REDUCTION_ARC_CMD_FENCE_WAIT")
	(g2fw::SCHED_GC_REDUCTION_ARC_CMD_LBW_WRITE,           "SCHED_GC_REDUCTION_ARC_CMD_LBW_WRITE")
	(g2fw::SCHED_GC_REDUCTION_ARC_CMD_LBW_BURST_WRITE,     "SCHED_GC_REDUCTION_ARC_CMD_LBW_BURST_WRITE")
	(g2fw::SCHED_GC_REDUCTION_ARC_CMD_FENCE_INC_IMMEDIATE, "SCHED_GC_REDUCTION_ARC_CMD_FENCE_INC_IMMEDIATE")
	(g2fw::SCHED_GC_REDUCTION_ARC_CMD_LBW_READ,            "SCHED_GC_REDUCTION_ARC_CMD_LBW_READ")
	(g2fw::SCHED_GC_REDUCTION_ARC_CMD_MEM_FENCE,           "SCHED_GC_REDUCTION_ARC_CMD_MEM_FENCE")
	(g2fw::SCHED_GC_REDUCTION_ARC_CMD_NOP,                 "SCHED_GC_REDUCTION_ARC_CMD_NOP")
	(g2fw::SCHED_GC_REDUCTION_ARC_CMD_ALLOC_NIC_BARRIER,   "SCHED_GC_REDUCTION_ARC_CMD_ALLOC_NIC_BARRIER")
	(g2fw::SCHED_GC_REDUCTION_ARC_CMD_NIC_EDMA_OPS,        "SCHED_GC_REDUCTION_ARC_CMD_NIC_EDMA_OPS")
    ;

    map_init(schedulersNames)
        (SchedulersIndex::dma,          "network_garbage_collector_and_reduction")
        (SchedulersIndex::sendScaleUp,  "scaleup_send")
        (SchedulersIndex::recvScaleUp,  "scaleup_receive")
        (SchedulersIndex::sendScaleOut, "scaleout_send")
        (SchedulersIndex::recvScaleOut, "scaleout_receive")
    ;

    int index = 0;
    for (auto& singleMap : smNames)
    {
        std::string indexStr = std::to_string(index++);
        map_init(singleMap)
            (SyncManagerName::networkMonitor, ("network_gp_monitors_"                    + indexStr))
            (SyncManagerName::longMonitor,    ("network_long_monitors_"                  + indexStr))
            (SyncManagerName::so,             ("network_gp_sos_"                         + indexStr))
            (SyncManagerName::cgInternal,     ("network_completion_queue_internal_"      + indexStr))
            (SyncManagerName::cgExternal,     ("network_completion_queue_external_"      + indexStr))
    ;
    }
}
// clang-format on

inline const std::string& ScalJsonNames::getCommandName(uint32_t opcode, uint32_t schedIdx)
{
    std::map<unsigned, std::string>* commandMap = nullptr;
    static std::string               invalid    = "<invalid>";

    switch (static_cast<SchedulersIndex>(schedIdx))
    {
        case SchedulersIndex::sendScaleUp:
            commandMap = &scaleupSendCmdName;
            break;
        case SchedulersIndex::recvScaleUp:
            commandMap = &scaleupRecvCmdName;
            break;
        case SchedulersIndex::sendScaleOut:
            commandMap = &scaleoutSendCmdName;
            break;
        case SchedulersIndex::recvScaleOut:
            commandMap = &scaleoutRecvCmdName;
            break;
        case SchedulersIndex::dma:
            commandMap = &dmaCmdName;
            break;
        default:
            LOG_WARN(HCL_SCAL, "Invalid schedIdx {} requested for parsing opcode {}", schedIdx, opcode);
            return invalid;
    }

    if (commandMap->count(opcode) == 0)
    {
        LOG_WARN(HCL_SCAL, "Invalid opcode {} in schedIdx {}", opcode, schedIdx);
        return invalid;
    }

    return commandMap->at(opcode);
}

inline const std::string ScalJsonNames::getFenceName(unsigned archStreamIdx, unsigned fenceIdx)
{
    return hostFenceNamePrefix + std::to_string(archStreamIdx) + std::to_string(fenceIdx);
}

}  // namespace hcl
