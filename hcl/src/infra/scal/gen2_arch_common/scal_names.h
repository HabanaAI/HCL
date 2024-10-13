#pragma once

#include <vector>
#include <string>
#include <map>
#include <array>
#include "hcl_utils.h"

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

enum class NetworkStreams
{
    reduceScatter = 0,
    allGather     = 1,
    arbitrator    = 2,
    max           = 3
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
     * ScalJsonNames is naming bound to the scal json configuration names.
       It hold all naming and maping and order for scal HCL SW layer needs.
     */

    ScalJsonNames();

    const std::string getFenceName(unsigned archStreamIdx, unsigned fenceIdx);

    std::map<SchedulersIndex, std::string>                                   schedulersNames;
    std::map<DMAStreams, std::string>                                        dmaStreamNames;
    std::map<NetworkStreams, std::string>                                    networkStreamNames;
    std::array<std::map<SyncManagerName, std::string>, numberOfArchsStreams> smNames;

    std::vector<unsigned> numberOfMicroArchStreams = {
        6,  // dma scheduler, streams: 0:cleanup, 1:reduction-scaleup, 2:arb, 3:reduction-scaleout, 4:signaling-stream,
            // 5:gaudi-direct
        3,  // scaleup send scheduler, streams: 0:RS, 1:AG, 2:arb
        3,  // scaleup recv scheduler, streams: 0:RS, 1:AG, 2:arb
        3,  // scaleout send scheduler, streams: 0:RS, 1:AG, 2:arb
        3   // scaleout recv scheduler, streams: 0:RS, 1:AG, 2:arb
    };

    const std::string hostFenceNamePrefix = "host_fence_counters_";
};

// clang-format off
inline ScalJsonNames::ScalJsonNames()
{
    map_init(schedulersNames)
        (SchedulersIndex::dma,          "network_garbage_collector_and_reduction")
        (SchedulersIndex::sendScaleUp,  "scaleup_send")
        (SchedulersIndex::recvScaleUp,  "scaleup_receive")
        (SchedulersIndex::sendScaleOut, "scaleout_send")
        (SchedulersIndex::recvScaleOut, "scaleout_receive")
    ;

    map_init(dmaStreamNames)
        (DMAStreams::garbageCollection, "gar")
        (DMAStreams::reduction,         "red")
        (DMAStreams::arbitrator,        "arb")
        (DMAStreams::scaleoutReduction, "sor")
        (DMAStreams::signaling,         "sig")
        (DMAStreams::gdr,               "gdr")
    ;

    map_init(networkStreamNames)
        (NetworkStreams::reduceScatter,  "rs")
        (NetworkStreams::allGather,      "ag")
        (NetworkStreams::arbitrator,     "arb")
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

inline const std::string ScalJsonNames::getFenceName(unsigned archStreamIdx, unsigned fenceIdx)
{
    return hostFenceNamePrefix + std::to_string(archStreamIdx) + std::to_string(fenceIdx);
}

}  // namespace hcl
