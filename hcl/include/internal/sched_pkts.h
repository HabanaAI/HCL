#pragma once

#include <cstdint>

// we include into a struct so we can later use a template to get to enums (and others),
// that are inside the "include"
struct g2fw
{
#include "gaudi2_arc_sched_packets.h"   // IWYU pragma: export
#include "gaudi2_arc_host_packets.h"    // IWYU pragma: export
#include "gaudi2_arc_common_packets.h"  // IWYU pragma: export
#include "gaudi2_arc_eng_packets.h"     // IWYU pragma: export
};

struct g3fw
{
#include "gaudi3/gaudi3_arc_sched_packets.h"   // IWYU pragma: export
#include "gaudi3/gaudi3_arc_host_packets.h"    // IWYU pragma: export
#include "gaudi3/gaudi3_arc_common_packets.h"  // IWYU pragma: export
#include "gaudi3/gaudi3_arc_eng_packets.h"     // IWYU pragma: export
};

#define SET_FIELD(field, value)                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        (field) = (value);                                                                                             \
        VERIFY((field) == (value), "The values 0x{:x},0x{:x} are not equal.", field, value);                           \
    } while (0);
