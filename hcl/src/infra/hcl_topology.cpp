#include "hcl_topology.h"
#include "hcl_utils.h"  // for VERIFY

static hwloc_topology_t create_topology()
{
    hwloc_topology_t topology = NULL;
    VERIFY((0 == hwloc_topology_init(&topology)), "Failed to initiate hwloc topology");
    VERIFY((0 == hwloc_topology_set_flags(topology, HWLOC_TOPOLOGY_FLAG_WHOLE_SYSTEM)),
           "Failed to set hwloc topology flags");
    VERIFY((0 == hwloc_topology_set_io_types_filter(topology, HWLOC_TYPE_FILTER_KEEP_ALL)),
           "Failed to set hwloc topology IO types filter");
    VERIFY((0 == hwloc_topology_load(topology)), "Failed to load hwloc topology");
    return topology;
}

HwlocTopology::HwlocTopology() : m_topology(create_topology()) {}

HwlocTopology::~HwlocTopology()
{
    try
    {
        hwloc_topology_destroy(m_topology);
    }
    catch (...)
    {
    }
}

hwloc_topology_t HwlocTopology::operator*() const
{
    return m_topology;
}
