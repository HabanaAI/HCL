/* SPDX-License-Identifier: MIT
 *
 * Copyright 2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */
#ifndef GAUDI3_ARC_FW_STM_EVENTS_H
#define GAUDI3_ARC_FW_STM_EVENTS_H

#include "profiler/gaudi3/gaudi3_global_stm_defs.h"

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
#define GAUDI3_MAX_SCHED_MIN_LEVEL_EVENTS             64
#define GAUDI3_MAX_ENG_MIN_LEVEL_EVENTS               32
#define GAUDI3_SCHED_LEVEL_MIN_EVENT_BASE(sched_idx)  ((sched_idx) * GAUDI3_MAX_SCHED_MIN_LEVEL_EVENTS)
#define GAUDI3_ENGINE_LEVEL_MIN_FIRST_EVENT           512
#define GAUDI3_ENGINE_LEVEL_MIN_EVENT_BASE(eng_idx)   (GAUDI3_ENGINE_LEVEL_MIN_FIRST_EVENT + (eng_idx) * GAUDI3_MAX_ENG_MIN_LEVEL_EVENTS)

/*
 * In the medium level, each scheduler engine can have
 * total of 64 events.
 * engine arcs can have up to 32 events, starting at event number 512.
 */
#define GAUDI3_MAX_SCHED_MED_LEVEL_EVENTS              64
#define GAUDI3_MAX_ENG_MED_LEVEL_EVENTS                32
#define GAUDI3_SCHED_LEVEL_MED_EVENT_BASE(sched_idx)  ((sched_idx) * GAUDI3_MAX_SCHED_MED_LEVEL_EVENTS)
#define GAUDI3_ENGINE_LEVEL_MED_FIRST_EVENT           512
#define GAUDI3_ENGINE_LEVEL_MED_EVENT_BASE(eng_idx)   (GAUDI3_ENGINE_LEVEL_MED_FIRST_EVENT + (eng_idx) * GAUDI3_MAX_ENG_MED_LEVEL_EVENTS)

/*
 * In the maximum level, each scheduler engine can have
 * total of 8 events.
 * engine arcs can have up to 8 events, starting at event number 64.
 */
#define GAUDI3_MAX_SCHED_MAX_LEVEL_EVENTS              8
#define GAUDI3_MAX_ENG_MAX_LEVEL_EVENTS                8
#define GAUDI3_SCHED_LEVEL_MAX_EVENT_BASE(sched_idx)  ((sched_idx) * GAUDI3_MAX_SCHED_MAX_LEVEL_EVENTS)
#define GAUDI3_ENGINE_LEVEL_MAX_FIRST_EVENT           64
#define GAUDI3_ENGINE_LEVEL_MAX_EVENT_BASE(eng_idx)   (GAUDI3_ENGINE_LEVEL_MAX_FIRST_EVENT + (eng_idx) * GAUDI3_MAX_ENG_MAX_LEVEL_EVENTS)

/*
 * base address of global STM in ARC CFG address space
 */
#define GAUDI3_D0_GLOBAL_STM_BASE_ADDR_ARC_FW 0x20000000
#define GAUDI3_D1_GLOBAL_STM_BASE_ADDR_ARC_FW 0x28000000
#define GAUDI3_GLOBAL_STM_BASE_ADDR_ARC_FW(die)  ((die == 0) ? GAUDI3_D0_GLOBAL_STM_BASE_ADDR_ARC_FW : GAUDI3_D1_GLOBAL_STM_BASE_ADDR_ARC_FW)

#define GAUDI3_ARC_FW_STM_ADDR(die, grp, ev) \
          GAUDI3_GLOBAL_STM_ADDR(GAUDI3_GLOBAL_STM_BASE_ADDR_ARC_FW(die), grp, ev)


/*
 * base address of global STM in Full address space
 */
#define GAUDI3_GLOBAL_STM_BASE_ADDR_ARC_FW_LBW(die)  ((die == 0) ? GAUDI3_D0_GLOBAL_STM_BASE_ADDR : GAUDI3_D1_GLOBAL_STM_BASE_ADDR)
#define GAUDI3_ARC_FW_STM_ADDR_LBW(die, grp, ev) \
                GAUDI3_GLOBAL_STM_ADDR(GAUDI3_GLOBAL_STM_BASE_ADDR_ARC_FW_LBW(die), grp, ev)

/*
 * define macros to calc STM address for various event
 * types, based on trace_cpuid and event index within the event type group.
 */
#define GAUDI3_SCHED_STATE_STM_ADDR(die, trace_cpuid, ev)                            \
        GAUDI3_ARC_FW_STM_ADDR(die,   \
                               gaudi3_global_stm_arc_fw_log_min,         \
                              GAUDI3_SCHED_LEVEL_MIN_EVENT_BASE(trace_cpuid) + (ev))

#define GAUDI3_SCHED_DCCM_EVENT_INDEX_BASE   32
#define GAUDI3_SCHED_DCCM_QUEUE_STM_ADDR(die, trace_cpuid, ev) \
        GAUDI3_SCHED_STATE_STM_ADDR(die, trace_cpuid, SCHED_DCCM_EVENT_INDEX_BASE + ev)

#define GAUDI3_ENGINE_CMD_STM_ADDR(die, trace_cpuid, ev)                           \
        GAUDI3_ARC_FW_STM_ADDR(die, \
                               gaudi3_global_stm_arc_fw_log_min,       \
                               GAUDI3_ENGINE_LEVEL_MIN_EVENT_BASE(trace_cpuid - TRACE_CPU_ID_DIE0_SCHED_MAX) + (ev))

#define GAUDI3_SCHED_CMD_STM_ADDR(die, trace_cpuid, ev)                                \
        GAUDI3_ARC_FW_STM_ADDR(die,     \
                               gaudi3_global_stm_arc_fw_log_med,           \
                               GAUDI3_SCHED_LEVEL_MED_EVENT_BASE(trace_cpuid) + (ev))

#define GAUDI3_ENGINE_SUB_CMD_STM_ADDR(die, trace_cpuid, ev)                       \
        GAUDI3_ARC_FW_STM_ADDR(die, \
                               gaudi3_global_stm_arc_fw_log_med,       \
                               GAUDI3_ENGINE_LEVEL_MED_EVENT_BASE(trace_cpuid - TRACE_CPU_ID_DIE0_SCHED_MAX) + (ev))

#define GAUDI3_SCHED_INSTANT_STM_ADDR(die, trace_cpuid, ev)                                \
        GAUDI3_ARC_FW_STM_ADDR(die,         \
                               gaudi3_global_stm_arc_fw_log_max,               \
                               GAUDI3_SCHED_LEVEL_MAX_EVENT_BASE(trace_cpuid) + (ev))

#define GAUDI3_SCHED_INSTANT_STM_ADDR_LBW(die, trace_cpuid, ev)                                \
        GAUDI3_ARC_FW_STM_ADDR_LBW(die,         \
                                gaudi3_global_stm_arc_fw_log_max,               \
                                GAUDI3_SCHED_LEVEL_MAX_EVENT_BASE(trace_cpuid) + (ev))

#define GAUDI3_ENGINE_INSTANT_STM_ADDR(die, trace_cpuid, ev)                                                     \
        GAUDI3_ARC_FW_STM_ADDR(die,                               \
                               gaudi3_global_stm_arc_fw_log_max,                                     \
                               GAUDI3_ENGINE_LEVEL_MAX_EVENT_BASE(trace_cpuid - TRACE_CPU_ID_DIE0_SCHED_MAX) + (ev))

#endif /* of ifndef GAUDI3_ARC_FW_STM_EVENTS_H */
