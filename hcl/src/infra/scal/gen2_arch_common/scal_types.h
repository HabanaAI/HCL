#pragma once

#include "scal_types.h"
#include "scal.h"
#include <cstdint>

static const unsigned HOST_FENCES_NR = 4;  // Number of host fences per ArchStream

namespace hcl
{
struct CgInfo
{
    unsigned size;
    uint64_t cgBaseAddr;
    unsigned cgIdx[5];
    unsigned nrOfIndices;
    unsigned cgIdxInHost;
    unsigned longSoDcore;
    unsigned longSoIndex;
    uint64_t longSoAddr;
    uint64_t longSoInitialValue;
    bool     forceOrder;
};

struct SmInfo
{
    unsigned soBaseIdx;
    unsigned soSize;
    unsigned soSmIndex;
    unsigned soDcoreIndex;
    unsigned monitorBaseIdx;
    unsigned monitorSmIndex;
    unsigned monitorSize;
    unsigned longMonitorBaseIdx;
    unsigned longMonitorSmIndex;
    unsigned longMonitorSize;
};

struct HostFenceInfo
{
    unsigned                 smIndex;
    unsigned                 smDcore;
};

struct InternalHostFenceInfo
{
    HostFenceInfo                    hostFenceInfo;
    const uint64_t*          decrementsPtr;
    volatile const uint64_t* incrementsPtr;
    scal_host_fence_counter_handle_t hostFenceCounterHandle;
};
}  // namespace hcl
