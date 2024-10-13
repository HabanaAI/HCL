/* SPDX-License-Identifier: MIT
 *
 * Copyright 2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */
#ifndef __GAUDI2_ARC_STM_H__
#define __GAUDI2_ARC_STM_H__

#include "gaudi2_arc_common_packets.h"

/**
 * Scheduler command events
 */
#define SCHED_STREAM_CMD_EVENT_ID	0
#define SCHED_DCCM_Q_CMD_EVENT_ID	1

/**
 * Scheduler DCCM Q events
 */
#define SCHED_DCCM_Q_EVENT_ID		0

/**
 * Scheduler instant events
 */
#define SCHED_INSTANT_EVENT_TYPE_ID		0
#define SCHED_INSTANT_EVENT_VALUE_ID	1
#define SCHED_INSTANT_EVENT_VALUE_SCHED_TYPE    2

enum sched_instant_events_t {
	SCHED_INST_EVENT_CPU_ID = 0,
	SCHED_INST_EVENT_OPCODE = 1,
	SCHED_INST_EVENT_COLLECT_TIMESTAMP = 2,
	SCHED_INST_EVENT_COUNT = 3
};

/**
 * Engine sub command events
 */
#define ENG_SUB_CMD_EVENT_ID		0

/**
 * Engine instant events
 */
#define ENG_INSTANT_EVENT_TYPE_ID		0
#define ENG_INSTANT_EVENT_VALUE_ID		1
#define ENG_INSTANT_EVENT_VALUE_ENG_TYPE    2

enum eng_compute_instant_events_t {
    ENG_CMPT_INST_EVENT_CPU_ID = 0,
    ENG_CMPT_INST_EVENT_DYN_LIST_SIZE = 1,
    ENG_CMPT_INST_EVENT_STATIC_LIST_SIZE = 2,
    ENG_CMPT_INST_EVENT_STATIC_SCHED_DMA_WAIT_START = 3,
    ENG_CMPT_INST_EVENT_STATIC_SCHED_DMA_WAIT_END = 4,
    ENG_CMPT_INST_EVENT_STATIC_CQ_WAIT_START = 5,
    ENG_CMPT_INST_EVENT_STATIC_CQ_WAIT_END = 6,
    ENG_CMPT_INST_EVENT_STATIC_CQ_FULL = 7,
    ENG_CMPT_INST_EVENT_DYN_CQ_FULL = 8,
    ENG_CMPT_INST_EVENT_COUNT = 9
};

enum eng_nic_instant_events_t {
    ENG_NIC_INST_EVENT_CPU_ID = 0,
    ENG_NIC_INST_EVENT_GLBL_CTXT_INSUFFICIENT_BYTES =1,
    ENG_NIC_INST_EVENT_COLL_CTXT_INSUFFICIENT_BYTES =2,
    ENG_NIC_INST_EVENT_SEND_RECV_NOP_INSUFFICIENT_BYTES =3,
    ENG_NIC_INST_EVENT_COLL_OPS_LONG_INSUFFICIENT_BYTES =4,
    ENG_NIC_INST_EVENT_COLL_OPS_RECV_INORDER_INSUFFICIENT_BYTES =5,
    ENG_NIC_INST_EVENT_SEND_RECV_INSUFFICIENT_BYTES =6,
    ENG_NIC_INST_EVENT_COLL_OPS_INSUFFICIENT_BYTES =7,
    ENG_NIC_INST_EVENT_COUNT = 8
};

/**
 * encoding for scheduler stream payload
 */
#define SCHED_STM_STREAM_PAYLOAD(stream_id, sched_type) \
	(((sched_type & 0x1F) << 6) | (stream_id & 0x3F))

#define SCHED_STM_INSTANT_EVENT_PAYLOAD(evt, val) \
	(((val & 0xFFFF) << 16) | (evt & 0xFFFF))

#define SCHED_STM_PAYLOAD_TO_STREAM_ID(payload) \
    ((payload) & 0x3F)

#define SCHED_STM_PAYLOAD_TO_SCHED_TYPE(payload) \
    (((payload) >> 6) & 0x1F)

#define SCHED_STM_PAYLOAD_TO_VAL(payload) \
    (((payload) >> 16) & 0xFFFF)

#define SCHED_STM_PAYLOAD_TO_EVENT_ID(payload) \
    ((payload) & 0xFFFF)

/**
 * encoding for engine command payload
 */
#define ENG_STM_CMD_PAYLOAD(dccm_q_id, eng_type) \
	(((eng_type & 0x1F) << 3) | (dccm_q_id & 0x7))

#define ENG_STM_PAYLOAD_TO_DCCM_Q_ID(payload) \
    ((payload) & 0x7)

#define ENG_STM_PAYLOAD_TO_ENG_TYPE(payload) \
    (((payload) >> 3) & 0x1F)

#define ENG_STM_PAYLOAD_TO_VAL(payload) \
    (((payload) >> 16) & 0xFFFF)

#define ENG_STM_PAYLOAD_TO_EVENT_ID(payload) \
    ((payload) & 0xFFFF)

#endif /* __GAUDI2_ARC_STM_H__ */
