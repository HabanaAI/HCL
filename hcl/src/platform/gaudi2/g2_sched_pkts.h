#pragma once

#include <cstdint>
#include "platform/gen2_arch_common/sched_pkts.h"  // for SET_FIELD

// we include into a struct so we can later use a template to get to enums (and others),
// that are inside the "include"
struct g2fw
{
#include "gaudi2_arc_sched_packets.h"   // IWYU pragma: export
#include "gaudi2_arc_host_packets.h"    // IWYU pragma: export
#include "gaudi2_arc_common_packets.h"  // IWYU pragma: export
#include "gaudi2_arc_eng_packets.h"     // IWYU pragma: export
#include "gaudi2_arc_fw_stm_events.h"
#include "gaudi2_arc_stm.h"
};
