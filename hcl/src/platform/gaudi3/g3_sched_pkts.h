#pragma once

#include <cstdint>
#include "platform/gen2_arch_common/sched_pkts.h"  // for SET_FIELD

// we include into a struct so we can later use a template to get to enums (and others),
// that are inside the "include"
struct g3fw
{
#include "gaudi3/gaudi3_arc_sched_packets.h"   // IWYU pragma: export
#include "gaudi3/gaudi3_arc_host_packets.h"    // IWYU pragma: export
#include "gaudi3/gaudi3_arc_common_packets.h"  // IWYU pragma: export
#include "gaudi3/gaudi3_arc_eng_packets.h"     // IWYU pragma: export
#include "gaudi3/gaudi3_arc_fw_stm_events.h"
#include "gaudi3/gaudi3_arc_stm.h"
};
