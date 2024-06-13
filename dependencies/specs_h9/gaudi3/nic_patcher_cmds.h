/* SPDX-License-Identifier: MIT
 *
 * Copyright 2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDI3_NIC_PATCHER_CMDS_H
#define GAUDI3_NIC_PATCHER_CMDS_H

#include <linux/types.h>
#include "gaudi3.h"

#define COLL_DESC_ENT_NICS_PER_ENTRY	24

#ifdef __cplusplus
// No unnamed structs in c++
#define PATCHER_CMD_FIELDS fields

namespace gaudi3
{
	namespace Nic
	{
#else
#define PATCHER_CMD_FIELDS
#endif

#pragma pack(push, 1)

/*
 * DB PATCHER / Collective Operations
 */

#define COLL_MSG_MAX_LEN	0xF0

/* The values of the message CMD field */
enum gaudi3_coll_cmd {
	COLL_CMD_CONSUME_DWORDS = 0,
	COLL_CMD_EXECUTE = 1,
	COLL_CMD_DESCRIPTOR_SND_RCV = 2,
	COLL_CMD_DESCRIPTOR_WRITE = 3,
	COLL_CMD_DESCRIPTOR_0_VOP_SR = 4,
	COLL_CMD_DESCRIPTOR_1_VOP_SR = 5,
	COLL_CMD_DESCRIPTOR_MS_SND_RCV = 6,
	COLL_CMD_DESCRIPTOR_MS_WRITE = 7,
	COLL_CMD_DEST_RANK_UPDATE = 8,
	COLL_CMD_LAST_RANK_UPDATE = 9,
	COLL_CMD_QP_OFFSET_UPDATE_LANE_0_1 = 10,
	COLL_CMD_QP_OFFSET_UPDATE_LANE_2_3 = 11,
	COLL_CMD_QP_BASE_UPDATE = 12,
	COLL_CMD_ENABLE_UPDATE = 13,
	COLL_CMD_MAX,
};

struct coll_desc_ent_ctrl {
	union {
		struct {
			__u32 update_bitmask:16;
			__u32 context_id:8;
			__u32 lane_select:4;
			__u32 cmd:4;
		};
		__u32 ctl;
	};
};

enum coll_desc_data_type {
	COLL_DESC_DATA_TYPE_REDUCTION = 0x0,
	COLL_DESC_DATA_TYPE_128_BYTE = 0x1,
	COLL_DESC_DATA_TYPE_256_BYTE = 0x2,
};

enum coll_desc_strategy {
	COLL_DESC_SPREAD_RESIDUE_LAST = 0x0,
	COLL_DESC_SPREAD_RESIDUE_ALL = 0x1,
};

struct coll_desc_ent_oper {
	union {
		struct {
			__u32 reduction_opcode:12;
			__u32 compression:1;
			__u32 data_type:2;
			__u32 strategy:1;
			__u32 opcode:5;
			__u32 rc:1;
			__u32 ack_req:1;
			__u32 reset_pipeline:1;
			__u32 rank_residue_sz:8;
		};
		__u32 opr;
	};
};

enum coll_desc_comp_type {
	COLL_DESC_COMP_NONE = 0x0,
	COLL_DESC_COMP_SOB = 0x1,
	COLL_DESC_COMP_CQ = 0x2,
	COLL_DESC_COMP_SOB_CQ = 0x3,
};

struct coll_desc_ent_comp {
	union {
		struct {
			__u32 sob_id:13;
			__u32 sub_sm:1;
			__u32 sm:3;
			__u32 mcid:7;
			__u32 alloc_h:1;
			__u32 cache_class:2;
			__u32 lso:1;
			__u32 so_cmd:2;
			__u32 completion_type:2;
		};
		__u32 comp;
	};
};

struct coll_desc_ent_nic_slice {
	union {
		struct {
			__u32 nic_size:24;
			__u32 nic_residue:8;
		};
		__u32 slice;
	};
};

/* Send/Receive descriptor */
struct coll_desc_send_receive {
		struct coll_desc_ent_ctrl ctrl;
		struct coll_desc_ent_oper oper;
		__u32 stride_between_ranks;
		struct coll_desc_ent_nic_slice nic;
		__u64 local_base_addr;
		__u32 local_tag;
		struct coll_desc_ent_comp comp;
};


enum coll_desc_axis_type {
	COLL_DESC_Z_AXIS = 0x0,
	COLL_DESC_X_AXIS = 0x1,
	COLL_DESC_Y_AXIS = 0x2,
	COLL_DESC_RESERVED = 0x3,
};


struct coll_desc_ent_axes {
	union {
		struct {
			__u32 pipeline_axis:2;
			__u32 rank_axis:2;
			__u32:28;
		};
		__u32 axes;
	};
};


/* Multi stride send/receive descriptor */
struct coll_desc_ms_send_receive {
		struct coll_desc_ent_ctrl ctrl;
		struct coll_desc_ent_oper oper;
		__u32 stride_between_ranks;
		struct coll_desc_ent_nic_slice nic;
		__u64 local_base_addr;
		__u32 local_tag;
		struct coll_desc_ent_comp comp;
		__u16 number_of_strides_1;
		__u16 number_of_strides_2;
		__u32 stride_size;
		__u32 stride_offset_1;
		__u32 stride_offset_2;
		struct coll_desc_ent_axes axes;
};

struct coll_desc_v_group_desc {
	__u32 end_addr;
	struct coll_desc_ent_nic_slice nic;
};

/* V operation send/receive descriptor */
struct coll_desc_v_operation_send_receive {
		struct coll_desc_ent_ctrl ctrl;
		struct coll_desc_ent_oper oper;
		__u64 local_base_addr;
		__u32 local_tag;
		struct coll_desc_ent_comp comp;
		struct coll_desc_v_group_desc groups[COLL_DESC_ENT_NICS_PER_ENTRY];
		__u8 rank_res[COLL_DESC_ENT_NICS_PER_ENTRY];
};


/* Write descriptor */
struct coll_desc_write {
	struct coll_desc_ent_ctrl ctrl;
	struct coll_desc_ent_oper oper;
	__u32 stride_between_ranks;
	struct coll_desc_ent_nic_slice nic;
	__u64 local_base_addr;
	__u32 local_tag;
	struct coll_desc_ent_comp local_comp;
	__u64 rem_base_addr;
	__u32 rem_tag;
	struct coll_desc_ent_comp rem_comp;
};

/* Update dest-rank descriptor */
struct coll_desc_ent_update_dest_rank {
	union {
		struct {
			__u32 nic_lane_0:8;
			__u32 nic_lane_1:8;
			__u32 nic_lane_2:8;
			__u32 nic_lane_3:8;
		};
		__u32 rank;
	};
};

struct coll_desc_update_dest_rank {
	struct coll_desc_ent_ctrl ctrl;
	struct coll_desc_ent_update_dest_rank nics[NIC_MAX_NUM_OF_MACROS];
};

/* Update QPn offset descriptors */
struct coll_desc_ent_update_qpn_offset_lo {
	union {
		struct {
			__u32 qpn_offset_lane_0:16;
			__u32 qpn_offset_lane_1:16;
		};
		__u32 lanes_lo;
	};
};

struct coll_desc_ent_update_qpn_offset_hi {
	union {
		struct {
			__u32 qpn_offset_lane_2:16;
			__u32 qpn_offset_lane_3:16;
		};
		__u32 lanes_hi;
	};
};

struct coll_desc_update_qpn_offset_lo {
	struct coll_desc_ent_ctrl ctrl;
	struct coll_desc_ent_update_qpn_offset_lo nics[NIC_MAX_NUM_OF_MACROS];
};

struct coll_desc_update_qpn_offset_hi {
	struct coll_desc_ent_ctrl ctrl;
	struct coll_desc_ent_update_qpn_offset_hi nics[NIC_MAX_NUM_OF_MACROS];
};

/* Update QPn-base descriptor */
struct coll_desc_update_qpn_base {
	struct coll_desc_ent_ctrl ctrl;
	__u32 base:24;
	__u32:8;
};

struct coll_desc_ent_ports {
	__u32 ports:24;
	__u32:8;
};

struct coll_desc_update_enable {
	struct coll_desc_ent_ctrl ctrl;
	struct coll_desc_ent_ports p_0_23;
	struct coll_desc_ent_ports p_24_47;
};

/* update last rank */
struct coll_desc_update_last_rank {
	struct coll_desc_ent_ctrl ctrl;
	struct coll_desc_ent_ports p_0_23;
	struct coll_desc_ent_ports p_24_47;
};

/* Consume space descriptor */
struct coll_desc_consume_space {
	union {
		struct {
			__u32 update_bitmask:16;
			__u32 lane_select:4;
			__u32 cmd:4;
			__u32 consume_dwords:8;
		};
		__u32 consume;
	};
};

/* exec descriptor */
struct coll_desc_exec {
	struct coll_desc_ent_ctrl ctrl;
};

/*
 * DB PATCHER / Direct collective operations
 */
struct direct_coll_desc_ent_ctrl {
	union {
		struct {
			__u32 qp:24;
			__u32 reserved:4;
			__u32 cmd:4;
		};
		__u32 ctl;
	};
};

struct direct_coll_desc_ent_ctrl_dwords {
	union {
		struct {
			__u32 reserved:20;
			__u32 dwords:8;
			__u32 cmd:4;
		};
		__u32 ctl;
	};
};

struct direct_coll_desc_ent_oper {
	union {
		struct {
			__u32 reduction_opcode:12;
			__u32 compression:1;
			__u32 read_clear:1;
			__u32 ack_req:1;
			__u32 opcode:5;
			__u32 rank_residue_sz:12;
		};
		__u32 opr;
	};
};

struct direct_coll_desc_misc_params {
	union {
		struct {
			__u32 ports_en:24;
			__u32 data_type:2;
			__u32 strategy:1;
			__u32 disregard_rank:1;
			__u32 disregard_lag:1;
			__u32 reserved:3;
		};
		__u32 config;
	};
};

struct direct_coll_desc_ent_dest_rank {
	union {
		struct {
			__u32 nic_lane_0:16;
			__u32 nic_lane_2:16;
		};
		__u16 dest_rank[2];
	};
};

struct direct_coll_desc_send_receive {
	union {
		struct {
			struct direct_coll_desc_ent_ctrl ctrl;
			struct direct_coll_desc_ent_oper oper;
			__u32 stride_between_ranks;
			struct coll_desc_ent_nic_slice nic;
			__u64 local_base_addr;
			struct direct_coll_desc_misc_params misc;
			struct coll_desc_ent_comp local_comp;
		} PATCHER_CMD_FIELDS;
		__u32 raw[8];
	};
};

struct direct_coll_desc_write {
	union {
		struct {
			struct direct_coll_desc_ent_ctrl ctrl;
			struct direct_coll_desc_ent_oper oper;
			__u32 stride_between_ranks;
			struct coll_desc_ent_nic_slice nic;
			__u64 local_base_addr;
			struct direct_coll_desc_misc_params misc;
			struct coll_desc_ent_comp local_comp;
			__u64 rem_base_addr;
			__u32 rem_tag;
			struct coll_desc_ent_comp rem_comp;
		} PATCHER_CMD_FIELDS;
		__u32 raw[12];
	};
};

struct direct_coll_desc_ms_send_receive {
	union {
		struct {
			struct direct_coll_desc_ent_ctrl ctrl;
			struct direct_coll_desc_ent_oper oper;
			__u32 stride_between_ranks;
			struct coll_desc_ent_nic_slice nic;
			__u64 local_base_addr;
			struct direct_coll_desc_misc_params misc;
			struct coll_desc_ent_comp local_comp;
			__u16 number_of_strides_1;
			__u16 number_of_strides_2;
			__u32 stride_size;
			__u32 stride_offset_1;
			__u32 stride_offset_2;
			struct coll_desc_ent_axes axes;
		} PATCHER_CMD_FIELDS;
		__u32 raw[13];
	};
};

/* V operation send/receive descriptor */
struct direct_coll_desc_v_operation_send_receive {
	union {
		struct {
			struct direct_coll_desc_ent_ctrl ctrl;
			struct direct_coll_desc_ent_oper oper;
			__u64 local_base_addr;
			struct direct_coll_desc_misc_params misc;
			struct coll_desc_ent_comp local_comp;
			struct coll_desc_v_group_desc groups[COLL_DESC_ENT_NICS_PER_ENTRY];
			__u8 rank_res[COLL_DESC_ENT_NICS_PER_ENTRY];
		} PATCHER_CMD_FIELDS;
		__u32 raw[60];
	};
};

struct direct_coll_desc_update_dest_rank {
	union {
		struct {
			struct direct_coll_desc_ent_ctrl ctrl;
			struct direct_coll_desc_ent_dest_rank nics[NIC_MAX_NUM_OF_MACROS];
		} PATCHER_CMD_FIELDS;
		__u32 raw[13];
	};
};

struct direct_coll_desc_update_last_rank {
	union {
		struct {
			struct direct_coll_desc_ent_ctrl ctrl;
			struct coll_desc_ent_ports p_0_23;
		} PATCHER_CMD_FIELDS;
		__u32 raw[2];
	};
};
#pragma pack(pop)

#ifdef __cplusplus
	} // namespace Nic
} // namespace gaudi3
#endif

#undef PATCHER_CMD_FIELDS
#endif /* GAUDI3_NIC_PATCHER_CMDS_H */
