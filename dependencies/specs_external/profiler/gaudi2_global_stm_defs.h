/* SPDX-License-Identifier: MIT
 *
 * Copyright 2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */
#ifndef _GAUDI2_GLOBAL_STM_DEFS_H
#define _GAUDI2_GLOBAL_STM_DEFS_H

/*
 * base address of global STM in device address space
 */
#define GAUDI2_GLOBAL_STM_BASE_ADDR  0x1000007ff4000000

/*
 * global STM events.
 * Gaudi2 has two masters in PSOC STM and has 128k stimulus ports.
 * The 128k ports are devided to 32 groups of 4k channels. Each group
 * can be enabled/disabled according to STMSPER register value,
 * bit 0 controls group 0 corresponding to channels 0, 32, 64, ...
 * bit 1 controls group 1 corresponding to channels 1, 33, 65, ...
 * bit 31 constrols group 31 corresponding to channels 31, 63, 95, ...
 */

/*
 * Calculate global STM port address based on group index and event
 * index within that group.
 */
#define GAUDI2_GLOBAL_STM_ADDR(base, grp, ev) \
        ((base) + (((ev) * 32 + (grp)) * 256))

#define GAUDI2_GLOBAL_STM_CHANNEL_TO_EVENT(ch)  (((ch) >> 5) & 0xfff)
#define GAUDI2_GLOBAL_STM_CHANNEL_TO_GROUP_IDX(ch)  ((ch) & 0x1f)


/*
 * currently used global STM groups
 * Groups used by scheduler and engine ARCs firmware
 */

#define gaudi2_global_stm_arc_fw_log_min (1)
#define gaudi2_global_stm_arc_fw_log_med (2)
#define gaudi2_global_stm_arc_fw_log_max (3)
#define gaudi2_global_stm_media_decoder (4)
#define gaudi2_global_stm_embedded_arc0_group (5)
#define gaudi2_global_stm_embedded_arc1_group (6)
#define gaudi2_global_stm_embedded_arm_group (7)

#endif /* of  _GAUDI2_GLOBAL_STM_DEFS_H */
