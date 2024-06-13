#pragma once

#include <hwloc.h>

class HwlocTopology final
{
public:
    HwlocTopology();
    virtual ~HwlocTopology();

    HwlocTopology(const HwlocTopology&) = delete;
    HwlocTopology(HwlocTopology&&)      = delete;

    hwloc_topology_t operator*() const;

private:
    const hwloc_topology_t m_topology;
};
