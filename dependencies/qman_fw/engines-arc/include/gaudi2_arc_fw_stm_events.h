/* SPDX-License-Identifier: MIT
 *
 * Copyright 2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */
#ifndef GAUDI2_ARC_FW_STM_EVENTS_H
#define GAUDI2_ARC_FW_STM_EVENTS_H

#include "profiler/gaudi2_global_stm_defs.h"

/*
 * STM events are grouped to three levels, min, medium and max verbosity.
 * each level can have up to 4096 events.
 * we split the events between the all engines.
 */


/*
 * In the minimum level, each scheduler engine can have
 * total of 64 events.
 * engine arcs can have up to 32 events, starting at event number 512.
 */
#define GAUDI2_MAX_SCHED_MIN_LEVEL_EVENTS             64
#define GAUDI2_MAX_ENG_MIN_LEVEL_EVENTS               32
#define GAUDI2_SCHED_LEVEL_MIN_EVENT_BASE(sched_idx)  ((sched_idx) * GAUDI2_MAX_SCHED_MIN_LEVEL_EVENTS)
#define GAUDI2_ENGINE_LEVEL_MIN_FIRST_EVENT           512
#define GAUDI2_ENGINE_LEVEL_MIN_EVENT_BASE(eng_idx)   (GAUDI2_ENGINE_LEVEL_MIN_FIRST_EVENT + (eng_idx) * GAUDI2_MAX_ENG_MIN_LEVEL_EVENTS)

/*
 * In the medium level, each scheduler engine can have
 * total of 64 events.
 * engine arcs can have up to 32 events, starting at event number 512.
 */
#define GAUDI2_MAX_SCHED_MED_LEVEL_EVENTS              64
#define GAUDI2_MAX_ENG_MED_LEVEL_EVENTS                32
#define GAUDI2_SCHED_LEVEL_MED_EVENT_BASE(sched_idx)  ((sched_idx) * GAUDI2_MAX_SCHED_MED_LEVEL_EVENTS)
#define GAUDI2_ENGINE_LEVEL_MED_FIRST_EVENT           512
#define GAUDI2_ENGINE_LEVEL_MED_EVENT_BASE(eng_idx)   (GAUDI2_ENGINE_LEVEL_MED_FIRST_EVENT + (eng_idx) * GAUDI2_MAX_ENG_MED_LEVEL_EVENTS)

/*
 * In the maximum level, each scheduler engine can have
 * total of 8 events.
 * engine arcs can have up to 8 events, starting at event number 64.
 */
#define GAUDI2_MAX_SCHED_MAX_LEVEL_EVENTS              8
#define GAUDI2_MAX_ENG_MAX_LEVEL_EVENTS                8
#define GAUDI2_SCHED_LEVEL_MAX_EVENT_BASE(sched_idx)  ((sched_idx) * GAUDI2_MAX_SCHED_MAX_LEVEL_EVENTS)
#define GAUDI2_ENGINE_LEVEL_MAX_FIRST_EVENT           64
#define GAUDI2_ENGINE_LEVEL_MAX_EVENT_BASE(eng_idx)   (GAUDI2_ENGINE_LEVEL_MAX_FIRST_EVENT + (eng_idx) * GAUDI2_MAX_ENG_MAX_LEVEL_EVENTS)

/*
 * base address of global STM in ARC CFG address space
 */
#define GAUDI2_GLOBAL_STM_BASE_ADDR_ARC_FW 0x24000000

#define GAUDI2_ARC_FW_STM_ADDR(grp, ev) \
          GAUDI2_GLOBAL_STM_ADDR(GAUDI2_GLOBAL_STM_BASE_ADDR_ARC_FW, grp, ev)

/*
 * define macros to calc STM address for various event
 * types, based on cpuid and event index within the event type group.
 */
#define GAUDI2_SCHED_STATE_STM_ADDR(cpuid, ev) \
        GAUDI2_ARC_FW_STM_ADDR(gaudi2_global_stm_arc_fw_log_min, \
                               GAUDI2_SCHED_LEVEL_MIN_EVENT_BASE(cpuid) + (ev))

#define GAUDI2_SCHED_DCCM_EVENT_INDEX_BASE   32
#define GAUDI2_SCHED_DCCM_QUEUE_STM_ADDR(cpuid, ev) \
        GAUDI2_SCHED_STATE_STM_ADDR(cpuid, SCHED_DCCM_EVENT_INDEX_BASE + ev)

#define GAUDI2_ENGINE_CMD_STM_ADDR(cpuid, ev) \
        GAUDI2_ARC_FW_STM_ADDR(gaudi2_global_stm_arc_fw_log_min, \
                               GAUDI2_ENGINE_LEVEL_MIN_EVENT_BASE((cpuid) - CPU_ID_SCHED_MAX) + (ev))

#define GAUDI2_SCHED_CMD_STM_ADDR(cpuid, ev) \
        GAUDI2_ARC_FW_STM_ADDR(gaudi2_global_stm_arc_fw_log_med, \
                               GAUDI2_SCHED_LEVEL_MED_EVENT_BASE(cpuid) + (ev))

#define GAUDI2_ENGINE_SUB_CMD_STM_ADDR(cpuid, ev) \
        GAUDI2_ARC_FW_STM_ADDR(gaudi2_global_stm_arc_fw_log_med, \
                               GAUDI2_ENGINE_LEVEL_MED_EVENT_BASE((cpuid) - CPU_ID_SCHED_MAX) + (ev))

#define GAUDI2_SCHED_INSTANT_STM_ADDR(cpuid, ev) \
        GAUDI2_ARC_FW_STM_ADDR(gaudi2_global_stm_arc_fw_log_max, \
                               GAUDI2_SCHED_LEVEL_MAX_EVENT_BASE(cpuid) + (ev))

#define GAUDI2_ENGINE_INSTANT_STM_ADDR(cpuid, ev) \
        GAUDI2_ARC_FW_STM_ADDR(gaudi2_global_stm_arc_fw_log_max, \
                               GAUDI2_ENGINE_LEVEL_MAX_EVENT_BASE((cpuid) - CPU_ID_SCHED_MAX) + (ev))

#endif /* of ifndef GAUDI2_ARC_FW_STM_EVENTS_H */
