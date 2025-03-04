/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
 *
 * Copyright 2022-2024 HabanaLabs, Ltd.
 * Copyright (C) 2023-2024, Intel Corporation.
 * All Rights Reserved.
 *
 */

#ifndef __HBLDV_H__
#define __HBLDV_H__

#include <stdbool.h>
#include <infiniband/verbs.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Number of backpressure offsets */
#define HBLDV_USER_BP_OFFS_MAX			16

/* Number of FnA addresses for SRAM/DCCM completion */
#define HBLDV_FNA_CMPL_ADDR_NUM			2

#define HBL_IB_MTU_8192				6

/* Maximum amount of Collective Scheduler resources */
#define HBLDV_MAX_NUM_COLL_SCHED_RESOURCES	256

/**
 * struct hbldv_qp_caps - HBL QP capabilities flags.
 * @HBLDV_QP_CAP_LOOPBACK: Enable QP loopback.
 * @HBLDV_QP_CAP_CONG_CTRL: Enable congestion control.
 * @HBLDV_QP_CAP_COMPRESSION: Enable compression.
 * @HBLDV_QP_CAP_SACK: Enable selective acknowledgment feature.
 * @HBLDV_QP_CAP_ENCAP: Enable packet encapsulation.
 * @HBLDV_QP_CAP_COLL: Enable collective operations.
 * @HBLDV_QP_CAP_MIGRATE: Migrate from old QP.
 */
enum hbldv_qp_caps {
	HBLDV_QP_CAP_LOOPBACK = 1 << 0,
	HBLDV_QP_CAP_CONG_CTRL = 1 << 1,
	HBLDV_QP_CAP_COMPRESSION = 1 << 2,
	HBLDV_QP_CAP_SACK = 1 << 3,
	HBLDV_QP_CAP_ENCAP = 1 << 4,
	HBLDV_QP_CAP_COLL = 1 << 5,
	HBLDV_QP_CAP_MIGRATE = 1 << 6,
};

/**
 * struct hbldv_port_ex_caps - HBL port extended capabilities flags.
 * @HBLDV_PORT_CAP_ADVANCED: Enable port advanced features like RDV, QMan, WTD, etc.
 */
enum hbldv_port_ex_caps {
	HBLDV_PORT_CAP_ADVANCED = 0x1,
};

/**
 * enum hbldv_mem_id - Gaudi2 (or higher) memory allocation methods.
 * @HBLDV_MEM_HOST: Memory allocated on the host.
 * @HBLDV_MEM_DEVICE: Memory allocated on the device.
 */
enum hbldv_mem_id {
	HBLDV_MEM_HOST = 1,
	HBLDV_MEM_DEVICE
};

/**
 * enum hbldv_wq_array_type - WQ-array type.
 * @HBLDV_WQ_ARRAY_TYPE_GENERIC: WQ-array for generic QPs.
 * @HBLDV_WQ_ARRAY_TYPE_COLLECTIVE: (Gaudi3 and above) WQ-array for collective QPs.
 * @HBLDV_WQ_ARRAY_TYPE_SCALE_OUT_COLLECTIVE: (Gaudi3 and above) WQ-array for scale-out
 *                                            collective QPs.
 * @HBLDV_WQ_ARRAY_TYPE_MAX: Max number of values in this enum.
 */
enum hbldv_wq_array_type {
	HBLDV_WQ_ARRAY_TYPE_GENERIC,
	HBLDV_WQ_ARRAY_TYPE_COLLECTIVE,
	HBLDV_WQ_ARRAY_TYPE_SCALE_OUT_COLLECTIVE,
	HBLDV_WQ_ARRAY_TYPE_MAX = 5
};

/**
 * enum hbldv_swq_granularity - send WQE granularity.
 * @HBLDV_SWQE_GRAN_32B: 32 byte WQE for linear write.
 * @HBLDV_SWQE_GRAN_64B: 64 byte WQE for multi-stride write.
 */
enum hbldv_swq_granularity {
	HBLDV_SWQE_GRAN_32B,
	HBLDV_SWQE_GRAN_64B
};

/**
 * enum hbldv_usr_fifo_type - NIC users FIFO modes of operation.
 * @HBLDV_USR_FIFO_TYPE_DB: (Gaudi2 and above) mode for direct user door-bell submit.
 * @HBLDV_USR_FIFO_TYPE_CC: (Gaudi2 and above) mode for congestion control.
 * @HBLDV_USR_FIFO_TYPE_COLL_OPS_SHORT: (Gaudi3 and above) mode for short collective operations.
 * @HBLDV_USR_FIFO_TYPE_COLL_OPS_LONG: (Gaudi3 and above) mode for long collective operations.
 * @HBLDV_USR_FIFO_TYPE_DWQ_LIN: (Gaudi3 and above) mode for linear direct WQE submit.
 * @HBLDV_USR_FIFO_TYPE_DWQ_MS: (Gaudi3 and above) mode for multi-stride WQE submit.
 * @HBLDV_USR_FIFO_TYPE_COLL_DIR_OPS_SHORT: (Gaudi3 and above) mode for direct short collective
 *                                          operations.
 * @HBLDV_USR_FIFO_TYPE_COLL_DIR_OPS_LONG: (Gaudi3 and above) mode for direct long collective
 *                                         operations.
 * @HBLDV_USR_FIFO_TYPE_LAG: (Fs1 and above) mode for lag operations.
 * @HBLDV_USR_FIFO_TYPE_LAG_COMPLETION: (Fs1 and above) mode for lag completion operations.
 */
enum hbldv_usr_fifo_type {
	HBLDV_USR_FIFO_TYPE_DB = 0,
	HBLDV_USR_FIFO_TYPE_CC,
	HBLDV_USR_FIFO_TYPE_COLL_OPS_SHORT,
	HBLDV_USR_FIFO_TYPE_COLL_OPS_LONG,
	HBLDV_USR_FIFO_TYPE_DWQ_LIN,
	HBLDV_USR_FIFO_TYPE_DWQ_MS,
	HBLDV_USR_FIFO_TYPE_COLL_DIR_OPS_SHORT,
	HBLDV_USR_FIFO_TYPE_COLL_DIR_OPS_LONG,
	HBLDV_USR_FIFO_TYPE_LAG,
	HBLDV_USR_FIFO_TYPE_LAG_COMPLETION,
};

/**
 * enum hbldv_qp_wq_types - QP WQ types.
 * @HBLDV_WQ_WRITE: WRITE or "native" SEND operations are allowed on this QP.
 *                  NOTE: the latter is currently unsupported.
 * @HBLDV_WQ_RECV_RDV: RECEIVE-RDV or WRITE operations are allowed on this QP.
 *                     NOTE: posting all operations at the same time is unsupported.
 * @HBLDV_WQ_READ_RDV: READ-RDV or WRITE operations are allowed on this QP.
 *                     NOTE: posting all operations at the same time is unsupported.
 * @HBLDV_WQ_SEND_RDV: SEND-RDV operation is allowed on this QP.
 * @HBLDV_WQ_READ_RDV_ENDP: No operation is allowed on this endpoint QP.
 */
enum hbldv_qp_wq_types {
	HBLDV_WQ_WRITE = 0x1,
	HBLDV_WQ_RECV_RDV = 0x2,
	HBLDV_WQ_READ_RDV = 0x4,
	HBLDV_WQ_SEND_RDV = 0x8,
	HBLDV_WQ_READ_RDV_ENDP = 0x10,
};

/**
 * enum hbldv_cq_type - CQ types, used during allocation of CQs.
 * @HBLDV_CQ_TYPE_QP: Standard CQ used for completion of a operation for a QP.
 * @HBLDV_CQ_TYPE_CC: Congestion control CQ.
 */
enum hbldv_cq_type {
	HBLDV_CQ_TYPE_QP = 0,
	HBLDV_CQ_TYPE_CC,
};

/**
 * enum hbldv_encap_type - Supported encapsulation types.
 * @HBLDV_ENCAP_TYPE_NO_ENC: No Tunneling.
 * @HBLDV_ENCAP_TYPE_ENC_OVER_IPV4: Tunnel RDMA packets through L3 layer.
 * @HBLDV_ENCAP_TYPE_ENC_OVER_UDP: Tunnel RDMA packets through L4 layer.
 */
enum hbldv_encap_type {
	HBLDV_ENCAP_TYPE_NO_ENC = 0,
	HBLDV_ENCAP_TYPE_ENC_OVER_IPV4,
	HBLDV_ENCAP_TYPE_ENC_OVER_UDP,
};

/**
 * enum hbldv_coll_sched_resource_type - FS1 (or higher) scheduler resource type.
 * @HBLDV_COLL_SCHED_RSRC_T_NMS_AF_UMR: Resource holding 33 pages mapping the NMS's AF's UMRs.
 * @HBLDV_COLL_SCHED_RSRC_T_NMS_AF: Resource holding 2 pages mapping the NMS's AF's configuration
 *                                  registers.
 * @HBLDV_COLL_SCHED_RSRC_T_NMS_ARC_AUX: Resource holding 2 pages mapping the NMS's AUX Registers
					 and the NSF GW's registers.
 * @HBLDV_COLL_SCHED_RSRC_T_NMS_ARC_DCCM: Resource holding 16 pages mapping the NMS ARC-DCCM.
 * @HBLDV_COLL_SCHED_RSRC_T_NMS_SDUP: Resource holding 8 pages mapping the NMS sDUP.
 * @HBLDV_COLL_SCHED_RSRC_T_NMS_SM: Resource holding 8 pages mapping of NMS SM.
 * @HBLDV_COLL_SCHED_RSRC_T_NMS_MFC_SLOTS: Resource holding 1 page of NMS MFC.
 * @HBLDV_COLL_SCHED_RSRC_T_NMS_EDUP_PUSH: Resource holding 1 page of NMS eDUP PUSH.
 * @HBLDV_COLL_SCHED_RSRC_T_NMS_EDUP_GRPS: Resource holding 1 page of NMS eDUP groups
 *                                         configuration.
 * @HBLDV_COLL_SCHED_RSRC_T_PDMA: Resource holding all the pages of PDMA on each of the compute
 *                                         dies.
 * @HBLDV_COLL_SCHED_RSRC_T_EDMA: Resource holding all the pages of EDMA on each of the compute
 *                                         dies.
 * @HBLDV_COLL_SCHED_RSRC_T_NIC_USR_FIFO_UMR: 4 NIC user-FIFO UMRs per NIC HW macro.
 * @HBLDV_COLL_SCHED_RSRC_T_NIC_SM_UMR: 1 Sync-Manager UMR containing the CQ CIs.
 * @HBLDV_COLL_SCHED_RSRC_T_MAX: Number of values in enum.
 */
enum hbldv_coll_sched_resource_type {
	HBLDV_COLL_SCHED_RSRC_T_NMS_AF_UMR,
	HBLDV_COLL_SCHED_RSRC_T_NMS_AF,
	HBLDV_COLL_SCHED_RSRC_T_NMS_ARC_AUX,
	HBLDV_COLL_SCHED_RSRC_T_NMS_ARC_DCCM,
	HBLDV_COLL_SCHED_RSRC_T_NMS_SDUP,
	HBLDV_COLL_SCHED_RSRC_T_NMS_SM,
	HBLDV_COLL_SCHED_RSRC_T_NMS_MFC_SLOTS,
	HBLDV_COLL_SCHED_RSRC_T_NMS_EDUP_PUSH,
	HBLDV_COLL_SCHED_RSRC_T_NMS_EDUP_GRPS,
	HBLDV_COLL_SCHED_RSRC_T_PDMA,
	HBLDV_COLL_SCHED_RSRC_T_EDMA,
	HBLDV_COLL_SCHED_RSRC_T_NIC_USR_FIFO_UMR,
	HBLDV_COLL_SCHED_RSRC_T_NMS_SM_UMR,
	HBLDV_COLL_SCHED_RSRC_T_MAX,
};

/**
 * enum hbldv_device_attr_caps - Device specific attributes.
 * @HBLDV_DEVICE_ATTR_CAP_CC: Congestion control.
 * @HBLDV_DEVICE_ATTR_CAP_COLL: Collective QPs.
 */
enum hbldv_device_attr_caps {
	HBLDV_DEVICE_ATTR_CAP_CC = 1 << 0,
	HBLDV_DEVICE_ATTR_CAP_COLL = 1 << 1,
};

/**
 * struct hbldv_ucontext_attr - HBL user context attributes.
 * @ports_mask: Mask of the relevant ports for this context (should be 1-based).
 * @core_fd: Core device file descriptor.
 */
struct hbldv_ucontext_attr {
	uint64_t ports_mask;
	int core_fd;
};

/**
 * struct hbldv_wq_array_attr - WQ-array attributes.
 * @max_num_of_wqs: Max number of WQs (QPs) to be used.
 * @max_num_of_wqes_in_wq: Max number of WQ elements in each WQ.
 * @mem_id: Memory allocation method.
 * @swq_granularity: Send WQE size.
 */
struct hbldv_wq_array_attr {
	uint32_t max_num_of_wqs;
	uint32_t max_num_of_wqes_in_wq;
	enum hbldv_mem_id mem_id;
	enum hbldv_swq_granularity swq_granularity;
};

/**
 * struct hbldv_port_ex_attr - HBL port extended attributes.
 * @wq_arr_attr: Array of WQ-array attributes for each WQ-array type.
 * @caps: Port capabilities bit-mask from hbldv_port_ex_caps.
 * @qp_wq_bp_offs: Offsets in NIC memory to signal a back pressure.
 * @atomic_fna_fifo_offs: SRAM/DCCM addresses provided to the HW by the user when FnA completion is
 *                        configured in the SRAM/DCCM.
 * @port_num: Port ID (should be 1-based).
 * @atomic_fna_mask_size: Completion address value mask.
 */
struct hbldv_port_ex_attr {
	struct hbldv_wq_array_attr wq_arr_attr[HBLDV_WQ_ARRAY_TYPE_MAX];
	uint64_t caps;
	uint32_t qp_wq_bp_offs[HBLDV_USER_BP_OFFS_MAX];
	uint32_t atomic_fna_fifo_offs[HBLDV_FNA_CMPL_ADDR_NUM];
	uint32_t port_num;
	uint8_t atomic_fna_mask_size;
};

/**
 * struct hbldv_query_port_attr - HBL query port specific parameters.
 * @max_num_of_qps: Number of QPs that are supported by the driver. User must allocate enough room
 *		    for his work-queues according to this number.
 * @num_allocated_qps: Number of QPs that were already allocated (in use).
 * @max_allocated_qp_num: The highest index of the allocated QPs (i.e. this is where the driver may
 *                        allocate its next QP).
 * @max_cq_size: Maximum size of a CQ buffer.
 * @max_num_of_scale_out_coll_qps: Number of scale-out collective QPs that are supported by the
 *                                 driver. User must allocate enough room for his collective
 *                                 work-queues according to this number.
 * @max_num_of_coll_qps: Number of collective QPs that are supported by the driver. User must
 *                       allocate enough room for his collective work-queues according to this
 *                       number.
 * @base_scale_out_coll_qp_num: The first scale-out collective QP id (common for all ports).
 * @base_coll_qp_num: The first collective QP id (common for all ports).
 * @coll_qps_offset: Specific port collective QPs index offset.
 * @advanced: true if advanced features are supported.
 * @max_num_of_cqs: Maximum number of CQs.
 * @max_num_of_usr_fifos: Maximum number of user FIFOs.
 * @max_num_of_encaps: Maximum number of encapsulations.
 * @nic_macro_idx: macro index of this specific port.
 * @nic_phys_port_idx: physical port index (AKA lane) of this specific port.
 */
struct hbldv_query_port_attr {
	uint32_t max_num_of_qps;
	uint32_t num_allocated_qps;
	uint32_t max_allocated_qp_num;
	uint32_t max_cq_size;
	uint32_t max_num_of_scale_out_coll_qps;
	uint32_t max_num_of_coll_qps;
	uint32_t base_scale_out_coll_qp_num;
	uint32_t base_coll_qp_num;
	uint32_t coll_qps_offset;
	uint8_t advanced;
	uint8_t max_num_of_cqs;
	uint8_t max_num_of_usr_fifos;
	uint8_t max_num_of_encaps;
	uint8_t nic_macro_idx;
	uint8_t nic_phys_port_idx;
};

/**
 * struct hbldv_qp_attr - HBL QP attributes.
 * @caps: QP capabilities bit-mask from hbldv_qp_caps.
 * @local_key: Unique key for local memory access. Needed for RTR state.
 * @remote_key: Unique key for remote memory access. Needed for RTS state.
 * @congestion_wnd: Congestion-Window size. Needed for RTS state.
 * @qp_num_hint: Explicitly request for specified QP id, valid for collective QPs.
 *               Needed for INIT state.
 * @dest_wq_size: Number of WQEs on the destination. Needed for RDV RTS state.
 * @wq_type: WQ type. e.g. write, rdv etc. Needed for INIT state.
 * @wq_granularity: WQ granularity [0 for 32B or 1 for 64B]. Needed for INIT state.
 * @priority: QoS priority. Needed for RTR and RTS state.
 * @coll_lag_idx: NIC index within LAG. Needed for collective QP RTS state.
 * @coll_last_in_lag: If last NIC in LAG. Needed for collective QP RTS state.
 * @encap_num: Encapsulation ID. Needed for RTS and RTS state.
 * @coll_lag_size: The collective LAG size (i.e. number of ports in this LAG).
 */
struct hbldv_qp_attr {
	uint64_t caps;
	uint32_t local_key;
	uint32_t remote_key;
	uint32_t congestion_wnd;
	uint32_t qp_num_hint;
	uint32_t dest_wq_size;
	enum hbldv_qp_wq_types wq_type;
	enum hbldv_swq_granularity wq_granularity;
	uint8_t priority;
	uint8_t coll_lag_idx;
	uint8_t coll_last_in_lag;
	uint8_t encap_num;
	uint8_t coll_lag_size;
	struct ibv_qp *qp_to_migrate;
};

/**
 * struct hbldv_query_qp_attr - Queried HBL QP data.
 * @qp_num: HBL QP num.
 * @swq_cpu_addr: Send WQ mmap address.
 * @rwq_cpu_addr: Receive WQ mmap address.
 */
struct hbldv_query_qp_attr {
	uint32_t qp_num;
	void *swq_cpu_addr;
	void *rwq_cpu_addr;
};

/**
 * struct hbldv_usr_fifo_attr - HBL user FIFO attributes.
 * @port_num: Port ID (should be 1-based).
 * @base_sob_addr: Base address of the sync object.
 * @num_sobs: Number of sync objects.
 * @usr_fifo_num_hint: Hint to allocate a specific usr_fifo HW resource.
 * @usr_fifo_type: FIFO Operation type.
 * @dir_dup_mask: (Gaudi3 and above) Ports for which the HW should duplicate the direct patcher
 *                descriptor.
 */
struct hbldv_usr_fifo_attr {
	uint32_t port_num;
	uint32_t base_sob_addr;
	uint32_t num_sobs;
	uint32_t usr_fifo_num_hint;
	enum hbldv_usr_fifo_type usr_fifo_type;
	uint8_t dir_dup_mask;
};

/**
 * struct hbldv_usr_fifo - HBL user FIFO.
 * @ci_cpu_addr: CI mmap address.
 * @regs_cpu_addr: UMR mmap address.
 * @regs_offset: UMR offset.
 * @usr_fifo_num: DB FIFO ID.
 * @size: Allocated FIFO size.
 * @bp_thresh: Backpressure threshold that was set by the driver.
 */
struct hbldv_usr_fifo {
	void *ci_cpu_addr;
	void *regs_cpu_addr;
	uint32_t regs_offset;
	uint32_t usr_fifo_num;
	uint32_t size;
	uint32_t bp_thresh;
};

/**
 * struct hbldv_cq_attr - HBL CQ attributes.
 * @port_num: Port number to which CQ is associated (should be 1-based).
 * @cq_type: Type of CQ to be allocated.
 */
struct hbldv_cq_attr {
	uint8_t port_num;
	enum hbldv_cq_type cq_type;
};

/**
 * struct hbldv_cq - HBL CQ.
 * @ibvcq: Verbs CQ.
 * @mem_cpu_addr: CQ buffer address.
 * @pi_cpu_addr: CQ PI memory address.
 * @regs_cpu_addr: CQ UMR address.
 * @cq_size: Size of the CQ.
 * @cq_num: CQ number that is allocated.
 * @regs_offset: CQ UMR reg offset.
 */
struct hbldv_cq {
	struct ibv_cq *ibvcq;
	void *mem_cpu_addr;
	void *pi_cpu_addr;
	void *regs_cpu_addr;
	uint32_t cq_size;
	uint32_t cq_num;
	uint32_t regs_offset;
};

/**
 * struct hbldv_query_cq_attr - HBL CQ.
 * @ibvcq: Verbs CQ.
 * @mem_cpu_addr: CQ buffer address.
 * @pi_cpu_addr: CQ PI memory address.
 * @regs_cpu_addr: CQ UMR address.
 * @cq_size: Size of the CQ.
 * @cq_num: CQ number that is allocated.
 * @regs_offset: CQ UMR reg offset.
 * @cq_type: Type of CQ resource.
 */
struct hbldv_query_cq_attr {
	struct ibv_cq *ibvcq;
	void *mem_cpu_addr;
	void *pi_cpu_addr;
	void *regs_cpu_addr;
	uint32_t cq_size;
	uint32_t cq_num;
	uint32_t regs_offset;
	enum hbldv_cq_type cq_type;
};

/**
 * struct hbldv_coll_qp_attr - HBL Collective QP attributes.
 * @is_scale_out: Is this collective connection for scale out.
 */
struct hbldv_coll_qp_attr {
	uint8_t is_scale_out;
};

/**
 * struct hbldv_coll_qp - HBL Collective QP.
 * @qp_num: collective qp num.
 */
struct hbldv_coll_qp {
	uint32_t qp_num;
};

/**
 * struct hbldv_encap_attr - HBL encapsulation specific attributes.
 * @tnl_hdr_ptr: Pointer to the tunnel encapsulation header. i.e. specific tunnel header data to be
 *               used in the encapsulation by the HW.
 * @tnl_hdr_size: Tunnel encapsulation header size.
 * @ipv4_addr: Source IP address, set regardless of encapsulation type.
 * @port_num: Port ID (should be 1-based).
 * @udp_dst_port: The UDP destination-port. Valid for L4 tunnel.
 * @ip_proto: IP protocol to use. Valid for L3 tunnel.
 * @encap_type: Encapsulation type. May be either no-encapsulation or encapsulation over L3 or L4.
 */
struct hbldv_encap_attr {
	uint64_t tnl_hdr_ptr;
	uint32_t tnl_hdr_size;
	uint32_t ipv4_addr;
	uint32_t port_num;
	union {
		uint16_t udp_dst_port;
		uint16_t ip_proto;
	};
	enum hbldv_encap_type encap_type;
};

/**
 * struct hbldv_encap - HBL DV encapsulation data.
 * @encap_num: HW encapsulation number.
 */
struct hbldv_encap {
	uint32_t encap_num;
};

/**
 * struct hbldv_cc_cq_attr - HBL congestion control CQ attributes.
 * @port_num: Port ID (should be 1-based).
 * @num_of_cqes: Number of CQ elements in CQ.
 */
struct hbldv_cc_cq_attr {
	uint32_t port_num;
	uint32_t num_of_cqes;
};

/**
 * struct hbldv_cc_cq - HBL congestion control CQ.
 * @mem_cpu_addr: CC CQ memory mmap address.
 * @pi_cpu_addr: CC CQ PI mmap address.
 * @cqe_size: CC CQ entry size.
 * @num_of_cqes: Number of CQ elements in CQ.
 */
struct hbldv_cc_cq {
	void *mem_cpu_addr;
	void *pi_cpu_addr;
	size_t cqe_size;
	uint32_t num_of_cqes;
};

/**
 * struct hbldv_coll_sched_resource - Collective Scheduler resource.
 * @type: Type of the resource.
 * @id: The resource's absolute index in the chip.
 *      If a resource has multiple copies in each NMS, each copy will be exposed with a unique ID
 *      that can be devised through NMS index * (copies per NMS) + copy index.
 * @size: The size of the resource.
 * @pa_offs: Offset of the resource relative to the LBW address base.
 * @virt_addr: Process memory mapped address of resource.
 */
struct hbldv_coll_sched_resource {
	enum hbldv_coll_sched_resource_type type;
	uint8_t id;
	size_t size;
	uint32_t pa_offs;
	void *virt_addr;
};

/**
 * struct hbldv_coll_sched_resource - Collective Scheduler resource.
 * @num_resources: How many resources have been allocated.
 * @rsrc: Array of allocated resources.
 */
struct hbldv_coll_sched_resources {
	int num_resources;
	struct hbldv_coll_sched_resource rsrc[HBLDV_MAX_NUM_COLL_SCHED_RESOURCES];
};

/**
 * struct hbldv_device_attr - Device specific attributes.
 * @caps: Capabilities mask.
 * @num_ports: Number of available ports.
 * @ext_ports_mask: Mask of IB indexes of relevant external ports for this context (1-based);
 *                  this is subset from all the available ports.
 * @hw_ports_mask: Mask of HW indexes of relevant ports for this context (0-based).
 */
struct hbldv_device_attr {
	uint64_t caps;
	uint8_t num_ports;
	uint64_t ext_ports_mask;
	uint64_t hw_ports_mask;
};

bool hbldv_is_supported(struct ibv_device *device);

struct ibv_context *hbldv_open_device(struct ibv_device *device, struct hbldv_ucontext_attr *attr);
int hbldv_query_device(struct ibv_context *context, struct hbldv_device_attr *attr);

int hbldv_set_port_ex(struct ibv_context *context, struct hbldv_port_ex_attr *attr);
/* port_num should be 1-based */
int hbldv_query_port(struct ibv_context *context, uint32_t port_num,
		     struct hbldv_query_port_attr *hbl_attr);

int hbldv_modify_qp(struct ibv_qp *ibqp, struct ibv_qp_attr *attr, int attr_mask,
		    struct hbldv_qp_attr *hbl_attr);
int hbldv_query_qp(struct ibv_qp *ibvqp, struct hbldv_query_qp_attr *qp_attr);
int hbldv_migrate_qp(struct ibv_qp *ibqp);
int hbldv_reserve_coll_qps(struct ibv_pd *ibvpd, struct hbldv_coll_qp_attr *coll_qp_attr,
			   struct hbldv_coll_qp *coll_qp);

struct hbldv_usr_fifo *hbldv_create_usr_fifo(struct ibv_context *context,
					     struct hbldv_usr_fifo_attr *attr);
int hbldv_destroy_usr_fifo(struct hbldv_usr_fifo *usr_fifo);

struct ibv_cq *hbldv_create_cq(struct ibv_context *context, int cqe,
			       struct ibv_comp_channel *channel, int comp_vector,
			       struct hbldv_cq_attr *cq_attr);
int hbldv_query_cq(struct ibv_cq *ibvcq, struct hbldv_query_cq_attr *hbl_cq);

struct hbldv_encap *hbldv_create_encap(struct ibv_context *context,
				       struct hbldv_encap_attr *encap_attr);
int hbldv_destroy_encap(struct hbldv_encap *hbl_encap);

int hbldv_reserve_coll_sched_resources(struct ibv_context *context,
				       struct hbldv_coll_sched_resources *sched_resrc);
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __HBLDV_H__ */
