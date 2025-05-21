#pragma once

#include <vector>
#include <string>
#include <map>
#include <array>
#include "hcl_utils.h"

namespace hcl
{
enum SchedulersIndex
{
    gp = 0,        // "network_garbage_collector_and_reduction"
    sendScaleUp,   // "scaleup_send"
    recvScaleUp,   // "scaleup_receive"
    sendScaleOut,  // "scaleout_send"
    recvScaleOut,  // "scaleout_receive"
    count,
};

enum class SyncManagerName
{
    networkMonitor = 0,  // "network_gp_monitors_"
    longMonitor,         // "network_long_monitors_"
    so,                  // "network_gp_sos_"
    cgInternal,          // "network_completion_queue_internal_"
    cgExternal,          // "network_completion_queue_external_"
    hfcMonitor,          // "network_gp_monitors_hfc_"
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
     * ScalJsonNames is naming bound to the scal json configuration names.
       It hold all naming and maping and order for scal HCL SW layer needs.
     */

    ScalJsonNames();

    const std::string getFenceName(unsigned archStreamIdx, unsigned fenceIdx);

    std::map<SchedulersIndex, std::string>                                   schedulersNames;
    std::array<std::map<SyncManagerName, std::string>, numberOfArchsStreams> smNames;

    const std::string hostFenceNamePrefix = "host_fence_counters_";
};

// clang-format off
inline ScalJsonNames::ScalJsonNames()
{
    map_init(schedulersNames)
        (SchedulersIndex::gp,          "network_garbage_collector_and_reduction")
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
            (SyncManagerName::networkMonitor, ("network_gp_monitors_"               + indexStr))
            (SyncManagerName::longMonitor,    ("network_long_monitors_"             + indexStr))
            (SyncManagerName::so,             ("network_gp_sos_"                    + indexStr))
            (SyncManagerName::cgInternal,     ("network_completion_queue_internal_" + indexStr))
            (SyncManagerName::cgExternal,     ("network_completion_queue_external_" + indexStr))
            (SyncManagerName::hfcMonitor,     ("network_gp_monitors_hfc_"           + indexStr))
    ;
    }
}
// clang-format on

inline const std::string ScalJsonNames::getFenceName(unsigned archStreamIdx, unsigned fenceIdx)
{
    return hostFenceNamePrefix + std::to_string(archStreamIdx) + std::to_string(fenceIdx);
}

}  // namespace hcl

constexpr unsigned SCHED_COUNT          = (unsigned)hcl::SchedulersIndex::count;
constexpr unsigned MAX_STREAM_PER_SCHED = 6;
