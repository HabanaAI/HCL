#include "platform/gaudi3/hcl_packets.h"

#include <cstring>  // for memset, size_t
#include <cstdint>  // for uint32_t, uint8_t
#include <map>      // for map

#include "define_synapse_common.hpp"  // for pdma context id
#include "gaudi3/gaudi3.h"            // for NIC_MAX_NUM_OF_MACROS
#include "gaudi3/nic_patcher_cmds.h"  // for direct_coll_desc_send_receive,direct_coll_desc_ent_ctrl_dwords
#include "hccl_types.h"               // for hcclRedOp_t
#include "hcl_global_conf.h"
#include "hcl_packets.h"
#include "hcl_types.h"
#include "hcl_utils.h"                                // for VERIFY
#include "infra/scal/gen2_arch_common/scal_stream.h"  // for ScalStreamBase
#include "infra/scal/gen2_arch_common/scal_names.h"
#include "platform/gaudi3/nic_passthrough_handler.h"  // for pRecordWithMetadataGaudi3
#include "platform/gen2_arch_common/hcl_packets_utils.h"
#include "platform/gen2_arch_common/types.h"  // for reduction_datat...
#include "scal.h"                             // for SCAL_NIC_RECEIV...
#include "g3_sched_pkts.h"                    // for g3fw
#include "synapse_profiler_api.hpp"           // for pdma context id
#include "hcl_math_utils.h"

#define DEFAULT_CACHE_ALLOC (g3fw::CACHE_ALLOC_NO)
#define DEFAULT_CACHE_CLASS (g3fw::CACHE_CLS_TOP)

#define REDUCT_OP_GET_EN(reductionOpCode)    (reductionOpCode & 0x1)
#define REDUCT_OP_GET_OP(reductionOpCode)    ((reductionOpCode >> 1) & 0x7)
#define REDUCT_OP_GET_RND(reductionOpCode)   ((reductionOpCode >> 4) & 0x3)
#define REDUCT_OP_GET_DT(reductionOpCode)    ((reductionOpCode >> 6) & 0xf)
#define REDUCT_OP_GET_DC(reductionOpCode)    ((reductionOpCode >> 10) & 0x3)
#define REDUCT_OP_GET_DT_UC(reductionOpCode) (REDUCT_OP_GET_DT(reductionOpCode) >= REDUCTION_UPSCALING_FP16)

// reduction and up-conversion (copied to the packet):
// [0]     – Reduction enable
// [3:1]   – Reduction operation
// [5:4]   – Reduction rounding mode
// [9:6]   – Reduction data type
// down conversion used for data read
// [11:10]  – down-conversion

union g3_nic_engine_reduction_opcode_t
{
    struct
    {
        struct
        {
            uint16_t Enabled : 1;
            uint16_t Op : 3;
            uint16_t RoundingMode : 2;
            uint16_t dataType : 4;
            uint16_t downConversion : 2;
            uint16_t pad : 4;
        };
    };
    uint16_t raw;
} __attribute__((aligned(2), __packed__));

enum reduction_operation_e
{
    REDUCTION_OP_ADDITION    = 0x0,
    REDUCTION_OP_SUBTRACTION = 0x1,
    REDUCTION_OP_MINIMUM     = 0x2,
    REDUCTION_OP_MAXIMUM     = 0x3,
    REDUCTION_OP_NONE        = 0x7
};

static constexpr reduction_operation_e getReductionOp(const hcclRedOp_t reduceOp)
{
    reduction_operation_e result = REDUCTION_OP_ADDITION;
    switch (reduceOp)
    {
        case hcclSum:
        case hcclProd:
        case hcclAvg:
        case hcclOpNone:
            result = REDUCTION_OP_ADDITION;
            break;
        case hcclMin:
            result = REDUCTION_OP_MINIMUM;
            break;
        case hcclMax:
            result = REDUCTION_OP_MAXIMUM;
            break;
    }
    return result;
};

static inline g3_nic_engine_reduction_opcode_t getReductionOpcode(HCL_CollectiveOp collectiveOp,
                                                                  hcclRedOp_t      reduceOp,
                                                                  hcclDataType_t   dataType,
                                                                  bool             isSend,
                                                                  bool             isScaleUp)
{
    static const std::map<hcclDataType_t, enum reduction_datatype_e> REDUCTION_MAP {
        {hcclInt8, REDUCTION_INT8},
        {hcclInt32, REDUCTION_INT32},
        {hcclUint8, REDUCTION_UINT8},
        {hcclUint32, REDUCTION_UINT32},
        {hcclBfloat16, REDUCTION_UPSCALING_BF16},
        {hcclFloat16, REDUCTION_UPSCALING_FP16},
        {hcclFloat32, REDUCTION_FP32}
    };

    g3_nic_engine_reduction_opcode_t result = {.raw = 0};
    if (dataType == hcclNumTypes)
    {
        return result;
    }

    result.dataType = REDUCTION_MAP.at(dataType);
    if ((collectiveOp != eHCLReduceScatter && collectiveOp != eHCLReduce) || !isSend)
    {
        return result;
    }

    switch (reduceOp)
    {
        case hcclSum:
            result.Enabled = 1;
            result.Op      = REDUCTION_OP_ADDITION;
            break;
        case hcclMax:
            result.Enabled = 1;
            result.Op      = REDUCTION_OP_MAXIMUM;
            break;
        case hcclMin:
            result.Enabled = 1;
            result.Op      = REDUCTION_OP_MINIMUM;
            break;
        case hcclOpNone:
            if (!isScaleUp && (dataType == hcclBfloat16 || dataType == hcclFloat16))
            {
                result.Enabled = 1;
                result.Op      = REDUCTION_OP_NONE;
            }
            else
            {
                result.Enabled = 0;
            }
            break;
        default:
            VERIFY(false, "Invalid reductioOp value {}", (int)reduceOp);
    }

    result.RoundingMode = REDUCTION_ROUND_HALF_TO_NEAREST_EVEN;

    result.downConversion = 0;  // 0x2 or 0x3 (down convert FP32 to BF16/FP16)

    return result;
}

static inline uint8_t getEngineGroupType(bool isSend, bool isScaleUp)
{
    static uint8_t grpTypes[4] = {SCAL_NIC_RECEIVE_SCALE_OUT_GROUP,
                                  SCAL_NIC_RECEIVE_SCALE_UP_GROUP,
                                  SCAL_NIC_SEND_SCALE_OUT_GROUP,
                                  SCAL_NIC_SEND_SCALE_UP_GROUP};
    return grpTypes[0x2 * isSend + 0x1 * isScaleUp];
}

static inline uint8_t getOpCode(bool isSend, bool isScaleUp)
{
    static uint8_t opCodes[4] = {g3fw::SCHED_SCALEOUT_RECV_ARC_CMD_NIC_PASSTHROUGH_V2,
                                 g3fw::SCHED_SCALEUP_RECV_ARC_CMD_NIC_PASSTHROUGH_V2,
                                 g3fw::SCHED_SCALEOUT_SEND_ARC_CMD_NIC_PASSTHROUGH_V2,
                                 g3fw::SCHED_SCALEUP_SEND_ARC_CMD_NIC_PASSTHROUGH_V2};
    return opCodes[0x2 * isSend + 0x1 * isScaleUp];
}

namespace SchedArcCommandsGaudi3
{
static void printCollectiveSendReceiveDescriptor(const HCL_CollectiveOp                            collectiveOp,
                                                 const bool                                        isSend,
                                                 const bool                                        isScaleUp,
                                                 const gaudi3::Nic::direct_coll_desc_send_receive* desc)
{
    LOG_DEBUG(HCL,
              "{}: collectiveOp = {}, isSend = {}, isScaleUp = {}, desc->fields.ctrl.cmd={},desc->fields.ctrl.qp={},\
                desc->fields.oper.reduction_opcode={}, desc->fields.oper.compression={}, desc->fields.oper.read_clear={},\
                desc->fields.oper.ack_req={}, desc->fields.oper.opcode={}, desc->fields.oper.rank_residue_sz={},\
                desc->fields.stride_between_ranks={}, desc->fields.nic.nic_size={}, desc->fields.nic.nic_residue={},\
                desc->fields.local_base_addr=0x{:x}, desc->fields.misc.ports_en={:024b}, desc->fields.misc.data_type={},\
                desc->fields.misc.strategy={}, desc->fields.misc.disregard_rank={}, desc->fields.misc.disregard_lag={},\
                desc->fields.local_comp.sob_id={}, desc->fields.local_comp.sub_sm={}, desc->fields.local_comp.sm={},\
                desc->fields.local_comp.mcid={}, desc->fields.local_comp.cache_class={}, desc->fields.local_comp.alloc_h={},\
                desc->fields.local_comp.lso={}, desc->fields.local_comp.so_cmd={}, desc->fields.local_comp.completion_type={}",
              __FUNCTION__,
              collectiveOp,
              isSend,
              isScaleUp,
              desc->fields.ctrl.cmd,
              desc->fields.ctrl.qp,
              desc->fields.oper.reduction_opcode,
              desc->fields.oper.compression,
              desc->fields.oper.read_clear,
              desc->fields.oper.ack_req,
              desc->fields.oper.opcode,
              desc->fields.oper.rank_residue_sz,
              desc->fields.stride_between_ranks,
              desc->fields.nic.nic_size,
              desc->fields.nic.nic_residue,
              desc->fields.local_base_addr,
              desc->fields.misc.ports_en,
              desc->fields.misc.data_type,
              desc->fields.misc.strategy,
              desc->fields.misc.disregard_rank,
              desc->fields.misc.disregard_lag,
              desc->fields.local_comp.sob_id,
              desc->fields.local_comp.sub_sm,
              desc->fields.local_comp.sm,
              desc->fields.local_comp.mcid,
              desc->fields.local_comp.cache_class,
              desc->fields.local_comp.alloc_h,
              desc->fields.local_comp.lso,
              desc->fields.local_comp.so_cmd,
              desc->fields.local_comp.completion_type);

    // desc->raw[]
    for (unsigned dwordIndex = 0; dwordIndex < 8; dwordIndex++)
    {
        LOG_TRACE(HCL, "{}: dword[{}]=0x{:x}", __FUNCTION__, dwordIndex, desc->raw[dwordIndex]);
    }
}

void serializeSendRecvDesc(const bool                                  isSend,
                           const bool                                  isScaleUp,
                           const uint32_t                              qpn,
                           const bool                                  disregardRank,
                           const uint64_t                              buff,
                           const uint64_t                              cellCount,
                           const bool                                  hasBufferSize,
                           const uint64_t                              count,
                           const uint8_t                               dcore,
                           const uint8_t                               ssm,
                           const uint16_t                              sobId,
                           const uint32_t                              ports_mask,
                           const HCL_CollectiveOp                      collectiveOp,
                           const hcclRedOp_t                           reduceOp,
                           const hcclDataType_t                        dataType,
                           const uint32_t                              ScaleupGroupSize,
                           const uint32_t                              lagSize,
                           const uint64_t                              strideCount,
                           gaudi3::Nic::direct_coll_desc_send_receive& desc)
{
    LOG_TRACE(
        HCL,
        "{}: collectiveOp = {}, isSend = {}, isScaleUp = {}, qpn = {}, disregardRank = {}, buff = 0x{:x}, cellCount = {}, hasBufferSize = {}, count = {},\
        dcore = {}, ssm = {}, sobId = {}, ports_mask = {:024b}, reduceOp = 0x{:x}, dataType = {}, ScaleupGroupSize = {}, lagSize={}, strideCount = {} ",
        __FUNCTION__,
        collectiveOp,
        isSend,
        isScaleUp,
        qpn,
        disregardRank,
        buff,
        cellCount,
        hasBufferSize,
        count,
        dcore,
        ssm,
        sobId,
        ports_mask,
        reduceOp,
        dataType,
        ScaleupGroupSize,
        lagSize,
        strideCount);

    //*************************DWORD 1 START****************************

    // Indicating the type of the descriptor/msg
    SET_FIELD(desc.fields.ctrl.cmd, gaudi3::Nic::COLL_CMD_DESCRIPTOR_SND_RCV);
    SET_FIELD(desc.fields.ctrl.qp, qpn);

    //*************************DWORD 2 START****************************

    // add the reduction opcode, look at g3_nic_engine_reduction_opcode_t for more information

    auto reductionOpcode = getReductionOpcode(collectiveOp, reduceOp, dataType, isSend, isScaleUp).raw;
    SET_FIELD(desc.fields.oper.reduction_opcode, reductionOpcode);

    // Valid if QPC.transport_type == RC use a compression
    SET_FIELD(desc.fields.oper.compression, (uint8_t)GCFG_HCL_USE_NIC_COMPRESSION.value());

    // Read Clear - Used in the AXI USER when fetching the message data from the memory AXI_USER.ATOMIC_FETCH_ANC_CLR =
    // WQE.RC
    SET_FIELD(desc.fields.oper.read_clear, 0);

    // Force AckReq
    SET_FIELD(desc.fields.oper.ack_req, 0);

    // WQE Opcode
    const uint8_t opc = isSend ? 2 /*SQ_WQE_OP_WR_LINEAR*/ : 5 /*SQ_WQE_OP_WR_RDV*/;
    SET_FIELD(desc.fields.oper.opcode, opc);

    // 12 bits indicating the residue size (in elements) to be added to the last NIC in the last Rank.
    // When the division of the message between all the Ranks does not divide evenly
    const uint64_t residue = hasBufferSize ? count - (cellCount * ScaleupGroupSize) : 0;
    SET_FIELD(desc.fields.oper.rank_residue_sz, residue);

    //*************************DWORD 3 START****************************

    // Indicating the size of the Rank in element
    if (isScaleUp && (collectiveOp != eHCLNoCollective))
    {
        SET_FIELD(desc.fields.stride_between_ranks, strideCount);
    }
    else
    {
        SET_FIELD(desc.fields.stride_between_ranks, cellCount);
    }

    //*************************DWORD 4 START****************************

    // Indicating the size of the NIC in elements
    SET_FIELD(desc.fields.nic.nic_size, cellCount / lagSize);

    // 8 bits indicating the residue size (in elements) to be added to the last NIC in the Rank.
    // When the division of the Rank size between all the NICs does not divide evenly
    SET_FIELD(desc.fields.nic.nic_residue, cellCount - (desc.fields.nic.nic_size * lagSize));

    //*************************DWORD 5 START****************************

    // Base address for the message. Should be used as part of the WQE address generation
    SET_FIELD(desc.fields.local_base_addr, buff);

    //*************************DWORD 6 START****************************

    // Indicating which ports are part of the collective descriptor. Port index is calculated as follows: Port0 =
    // cfg.NIC_ID * 2, Port1 = cfg.NIC_ID * 2 + 1.
    SET_FIELD(desc.fields.misc.ports_en, ports_mask);

    // In order to determine the data type that should be used to calculate the address and size of the WQE,
    // the patcher should consider the ports.data_type and the reduction_opcode[9:6].
    // 16 bit data types use 128 Byte and 32 bit data types use 256 Byte
    // 0x0: As written in the reduction_opcode[9:6]
    // 0x1: 128Byte
    // 0x2: 256Byte
    // 0x3: Reserved
    SET_FIELD(desc.fields.misc.data_type, 0);

    // Strategy for residue spreading
    // 0x0 – Spread residue at the last NIC of the RANK and the Last RANK of the message
    // 0x1 – Spread residue between NICs in the RANK (starting at the first NIC Index in the LAG) Spread residue between
    // RANKSs (Starting with the first RANK in the message and using the Last NIC in each RANK) Currently only
    // supporting mode 0x0
    SET_FIELD(desc.fields.misc.strategy, 0);

    // Disregard Rank – indicating the patcher to disregard the RANK set in the QP and use the value of Zero instead
    SET_FIELD(desc.fields.misc.disregard_rank, disregardRank);

    // Disregard Lag – indicating to the Patcher to disregard the Lag index set in the QP and instead calculate it from
    // the enable_bit map. New_Lag_index = count_one(desc.enable_bit[port_id-1:0])
    SET_FIELD(desc.fields.misc.disregard_lag, 0);

    //*************************DWORD 7 START****************************

    // Sync object id that will be signaled on the responder.
    // This field is copied to the packet.
    // sync_object_address = cfg_sm_base_address[WQE.SM_ID] + LSSM*cfg.sub_sm_offset + WQE.sync_object_id *4
    SET_FIELD(desc.fields.local_comp.sob_id, sobId);

    // Local/Remote Sub Sync Manager. In each sync manager there 2 Sub sync managers 0 and 1. This bit selects between
    // them
    SET_FIELD(desc.fields.local_comp.sub_sm, ssm);

    // Local sync manager ID. Used for sync manager base address selection. This field is copied to the packet.
    SET_FIELD(desc.fields.local_comp.sm, dcore);

    // Copied to the AXI user bits when fetching the message data from the memory.
    SET_FIELD(desc.fields.local_comp.mcid, 0);

    // Copied to the AXI user bits when fetching the message data from the memory
    SET_FIELD(desc.fields.local_comp.cache_class, 0);

    // Local alloc - Indication to the cache to allocate a cacheline for the transaction.
    // this field means we use intermidiate buffer
    SET_FIELD(desc.fields.local_comp.alloc_h, (desc.fields.oper.reduction_opcode & 0x1) ? 1 : 0);

    // Long sync object. This field is copied to the packet
    SET_FIELD(desc.fields.local_comp.lso, 0);

    // This field is copied to the packet
    // Sync object command.
    // 0x0 – set to zero (AXI.data[31] = 0x0, AXI.data[30:25]= cfg, AXI.data[24]=WQE.LSO, AXI[23:0] = 0x0)
    // 0x1 – set to tag   (AXI.data[31] = 0x0, AXI.data[30:25]= cfg, AXI.data[24]=WQE.LSO, AXI[23:0] = wqe.tag[23:0])
    // 0x2 – increment  (AXI.data[31] = 0x1, AXI.data[30:25]= cfg, AXI.data[24]=WQE.LSO, AXI[23:0] = 0x1)
    // 0x3 – decrement (AXI.data[31] = 0x1, AXI.data[30:25]= cfg, AXI.data[24]=WQE.LSO, AXI[23:0] = 0xFFFFFF)
    SET_FIELD(desc.fields.local_comp.so_cmd, 0x2);

    // Completion type
    // 0x0 – No Sync object update and no completion queue update
    // 0x1 – Sync object update but no completion queue update
    // 0x2 – No Sync object update but Do completion queue update
    // 0x3 – Sync object update and completion queue update
    SET_FIELD(desc.fields.local_comp.completion_type, 0x1);

    printCollectiveSendReceiveDescriptor(collectiveOp, isSend, isScaleUp, &desc);
}

void serializeCollectiveCommand(hcl::ScalStreamBase& scalStream,
                                bool                 isSend,
                                bool                 isScaleUp,
                                uint32_t             qpn,
                                bool                 disregardRank,
                                uint64_t             buff,
                                uint64_t             cellCount,
                                bool                 hasBufferSize,
                                uint64_t             count,
                                uint8_t              dcore,
                                uint8_t              ssm,
                                uint16_t             sobId,
                                uint32_t             ports_mask,
                                HCL_CollectiveOp     collectiveOp,
                                hcclRedOp_t          reduceOp,
                                hcclDataType_t       dataType,
                                uint32_t             ScaleupGroupSize,
                                uint32_t             lagSize,
                                uint64_t             strideCount)
{
    // calc the command size
    const size_t size =
        (sizeof(g3fw::sched_arc_cmd_nic_passthrough_v2_t) + sizeof(gaudi3::Nic::direct_coll_desc_send_receive));
    g3fw::sched_arc_cmd_nic_passthrough_v2_t* command =
        reinterpret_cast<g3fw::sched_arc_cmd_nic_passthrough_v2_t*>(scalStream.getNextPtr(size));
    memset(command, 0, size);

    uint16_t residue = 0;
    if (hasBufferSize)
    {
        residue = count - (cellCount * ScaleupGroupSize);
    }
    PRINT_PACKET_TRACE(
        scalStream,
        "collectiveOp = {}, isSend = {}, isScaleUp = {}, qpn = {}, disregardRank = {}, buff = 0x{:x}, cellCount = {}, residue={}, hasBufferSize = {}, count = {}, \
        dcore = {}, ssm = {}, sobId = {}, ports_mask = {:024b}, reduceOp = 0x{:x}, dataType = {}, ScaleupGroupSize = {}, lagSize = {}, strideCount = {}",
        collectiveOp,
        isSend,
        isScaleUp,
        qpn,
        disregardRank,
        buff,
        cellCount,
        residue,
        hasBufferSize,
        count,
        dcore,
        ssm,
        sobId,
        ports_mask,
        reduceOp,
        dataType,
        ScaleupGroupSize,
        lagSize,
        strideCount);

    // fill in sched_arc_cmd_nic_passthrough_v2_t
    auto opCode = getOpCode(isSend, isScaleUp);
    SET_FIELD(command->opcode, opCode);
    auto engineGroupType = getEngineGroupType(isSend, isScaleUp);
    SET_FIELD(command->engine_group_type, engineGroupType);
    SET_FIELD(command->num_dwords, sizeof(gaudi3::Nic::direct_coll_desc_send_receive) >> 2);

    gaudi3::Nic::direct_coll_desc_send_receive* desc =
        (gaudi3::Nic::direct_coll_desc_send_receive*)command->passthrough_data;

    serializeSendRecvDesc(isSend,
                          isScaleUp,
                          qpn,
                          disregardRank,
                          buff,
                          cellCount,
                          hasBufferSize,
                          count,
                          dcore,
                          ssm,
                          sobId,
                          ports_mask,
                          collectiveOp,
                          reduceOp,
                          dataType,
                          ScaleupGroupSize,
                          lagSize,
                          strideCount,
                          *desc);
}

void serializeScaleupNonCollectiveCommand(hcl::ScalStreamBase& scalStream,
                                          const bool           isSend,
                                          const uint32_t       qpn,
                                          const uint64_t       buff,
                                          const uint64_t       count,
                                          const uint8_t        dcore,
                                          const uint8_t        ssm,
                                          const uint16_t       sobId,
                                          const uint32_t       ports_mask,
                                          const hcclDataType_t dataType,
                                          const unsigned       maxNumScaleUpNicsPerConnection)
{
    // calc the command size
    constexpr size_t size =
        (sizeof(g3fw::sched_arc_cmd_nic_passthrough_v2_t) + sizeof(gaudi3::Nic::direct_coll_desc_send_receive));
    g3fw::sched_arc_cmd_nic_passthrough_v2_t* command =
        reinterpret_cast<g3fw::sched_arc_cmd_nic_passthrough_v2_t*>(scalStream.getNextPtr(size));
    memset(command, 0, size);

    PRINT_PACKET_TRACE(scalStream,
                       "isSend = {}, qpn = {}, buff = 0x{:x}, count = {}, \
                        dcore = {}, ssm = {}, sobId = {}, ports_mask = {:024b}, \
                        dataType = {}, maxNumScaleUpNicsPerConnection={}",
                       isSend,
                       qpn,
                       buff,
                       count,
                       dcore,
                       ssm,
                       sobId,
                       ports_mask,
                       dataType,
                       maxNumScaleUpNicsPerConnection);

    // fill in sched_arc_cmd_nic_passthrough_v2_t
    constexpr bool isScaleUp = true;
    auto           opCode    = getOpCode(isSend, isScaleUp);
    SET_FIELD(command->opcode, opCode);
    auto engineGroupType = getEngineGroupType(isSend, isScaleUp);
    SET_FIELD(command->engine_group_type, engineGroupType);
    SET_FIELD(command->num_dwords, sizeof(gaudi3::Nic::direct_coll_desc_send_receive) >> 2);

    gaudi3::Nic::direct_coll_desc_send_receive* desc =
        (gaudi3::Nic::direct_coll_desc_send_receive*)command->passthrough_data;

    serializeSendRecvDesc(isSend,
                          isScaleUp,
                          qpn,
                          true /* disregardRank */,
                          buff,
                          count,
                          false /* hasBufferSize */,
                          count,
                          dcore,
                          ssm,
                          sobId,
                          ports_mask,
                          eHCLNoCollective,
                          hcclOpNone,
                          dataType,
                          0 /* ScaleupGroupSize not used */,
                          maxNumScaleUpNicsPerConnection, /* lagSize - check with enginefw */
                          0 /* strideCount not used */,
                          *desc);
}

void serializeNicPassthroughCommand(hcl::ScalStreamBase&             scalStream,
                                    const bool                       isSend,
                                    const uint32_t                   credits,
                                    const pRecordWithMetadataGaudi3& record)
{
    // calc the command size: PW + DW
    const size_t size = sizeof(g3fw::sched_arc_cmd_nic_passthrough_v2_t) + record->m_numDwords * sizeof(uint32_t);
    g3fw::sched_arc_cmd_nic_passthrough_v2_t* command =
        reinterpret_cast<g3fw::sched_arc_cmd_nic_passthrough_v2_t*>(scalStream.getNextPtr(size));
    memset(command, 0, size);

    PRINT_PACKET_TRACE(scalStream,
                       "isSend = {}, credits={}, size={}, dupMask={:012b}",
                       isSend,
                       credits,
                       size,
                       record->m_dupMask);

    // fill in sched_arc_cmd_nic_passthrough_v2_t
    constexpr bool isScaleUp = true;
    auto           opCode    = getOpCode(isSend, isScaleUp);
    SET_FIELD(command->opcode, opCode);
    auto engineGroupType = getEngineGroupType(isSend, isScaleUp);
    SET_FIELD(command->engine_group_type, engineGroupType);
    SET_FIELD(command->num_dwords, record->m_numDwords);
    SET_FIELD(command->dup_mask, record->m_dupMask);
    SET_FIELD(command->required_q_credits_inbytes, credits);

    const uint32_t commandData = *((const uint32_t*)command);

    gaudi3::Nic::direct_coll_desc_send_receive* desc =
        (gaudi3::Nic::direct_coll_desc_send_receive*)command->passthrough_data;
    memcpy(&(desc->raw[0]), &(record->m_payload), record->m_numDwords << 2);  // copy the payload
    LOG_TRACE(HCL, "{}:: commandData=0x{:x}", __FUNCTION__, commandData);

    if (unlikely(LOG_LEVEL_AT_LEAST_TRACE(HCL)))
    {
        for (size_t payLoadIndex = 0; payLoadIndex < record->m_numDwords; payLoadIndex++)
        {
            LOG_TRACE(HCL, "{}:: payload[{}]=0x{:x}", __FUNCTION__, payLoadIndex, desc->raw[payLoadIndex]);
        }
    }
}

void serializeNicNopCommand(hcl::ScalStreamBase& scalStream,
                            const bool           isSend,
                            const uint16_t       dupMask,
                            const uint32_t       credits,
                            const uint32_t       consumeDwords)
{
    // calc the command size: PW + single DW
    constexpr size_t nopCmdDwords = sizeof(gaudi3::Nic::direct_coll_desc_ent_ctrl_dwords) / sizeof(uint32_t);
    constexpr size_t size         = sizeof(g3fw::sched_arc_cmd_nic_passthrough_v2_t) +
                            sizeof(gaudi3::Nic::direct_coll_desc_ent_ctrl_dwords);  // bytes
    g3fw::sched_arc_cmd_nic_passthrough_v2_t* command =
        reinterpret_cast<g3fw::sched_arc_cmd_nic_passthrough_v2_t*>(scalStream.getNextPtr(size));
    memset(command, 0, size);

    PRINT_PACKET_TRACE(scalStream,
                       "isSend = {}, credits={}, consumeDwords={}, size={}, dupMask={:012b}",
                       isSend,
                       credits,
                       consumeDwords,
                       size,
                       dupMask);

    // fill in sched_arc_cmd_nic_passthrough_v2_t
    constexpr bool isScaleUp = true;
    auto           opCode    = getOpCode(isSend, isScaleUp);
    SET_FIELD(command->opcode, opCode);
    auto engineGroupType = getEngineGroupType(isSend, isScaleUp);
    SET_FIELD(command->engine_group_type, engineGroupType);
    SET_FIELD(command->num_dwords, nopCmdDwords);
    SET_FIELD(command->dup_mask, dupMask);
    SET_FIELD(command->required_q_credits_inbytes, credits);

    const uint32_t commandData = *((const uint32_t*)command);

    gaudi3::Nic::direct_coll_desc_ent_ctrl_dwords* desc =
        (gaudi3::Nic::direct_coll_desc_ent_ctrl_dwords*)command->passthrough_data;

    // Fill direct_coll_desc_ent_ctrl_dwords
    SET_FIELD(desc->cmd, 0);
    SET_FIELD(desc->dwords, consumeDwords);
    const uint32_t descData = desc->ctl;

    LOG_TRACE(HCL, "{}:: commandData=0x{:x} NOP descData=0x{:x}", __FUNCTION__, commandData, descData);
}

void serializeGlobalDmaCommand(hcl::ScalStreamBase&                  scalStream,
                               [[maybe_unused]] uint32_t             soAddressLSB,
                               const std::vector<sibAddressAndSize>& sibAddressesAndSizes,
                               uint32_t                              fwStrideSize,
                               uint64_t                              fwBaseAddress,
                               uint32_t                              engineType)
{
    const unsigned numDwords            = div(sizeof(g3fw::edma_nic_glbl_ctxt_v3_t), sizeof(uint32_t));
    const unsigned activateAllDwordsMap = (1 << numDwords) - 1;
    // sched_arc_cmd_nic_edma_ops_t with arc_cmd_update_edma_nic_ctxt_v3_t
    // and edma_nic_glbl_ctxt_v3_t
    const size_t sizeInBytes = sizeof(g3fw::sched_arc_cmd_nic_edma_ops_t) +
                               sizeof(g3fw::arc_cmd_update_edma_nic_ctxt_v3_t) + (numDwords * sizeof(uint32_t));

    g3fw::sched_arc_cmd_nic_edma_ops_t* command =
        reinterpret_cast<g3fw::sched_arc_cmd_nic_edma_ops_t*>(scalStream.getNextPtr(sizeInBytes));
    memset(command, 0, sizeInBytes);

    SET_FIELD(command->opcode, g3fw::SCHED_GC_REDUCTION_ARC_CMD_NIC_EDMA_OPS);
    SET_FIELD(command->cmd_size, sizeInBytes);
    SET_FIELD(command->engine_group_type, engineType);

    struct g3fw::arc_cmd_update_edma_nic_ctxt_v3_t* edma_ops =
        (struct g3fw::arc_cmd_update_edma_nic_ctxt_v3_t*)&command->edma_ctxt_v3;

    SET_FIELD(edma_ops->opcode, g3fw::NIC_EDMA_CMD_UPDATE_GLBL_CTXT_V3);
    SET_FIELD(edma_ops->update_bitmap, activateAllDwordsMap);
    SET_FIELD(edma_ops->num_dwords, numDwords);
    SET_FIELD(edma_ops->sob_address, 0);

    struct g3fw::edma_nic_glbl_ctxt_v3_t* edma_ctxt = (struct g3fw::edma_nic_glbl_ctxt_v3_t*)&edma_ops->data;

    SET_FIELD(edma_ctxt->sib_base_addr[0], sibAddressesAndSizes.at(0).sibBaseAddr);
    SET_FIELD(edma_ctxt->sib_base_addr[1], sibAddressesAndSizes.at(1).sibBaseAddr);
    SET_FIELD(edma_ctxt->sibo_rank_stride[0], sibAddressesAndSizes.at(0).sibSize);
    SET_FIELD(edma_ctxt->sibo_rank_stride[1], sibAddressesAndSizes.at(1).sibSize);
    SET_FIELD(edma_ctxt->sirb_base_addr, fwBaseAddress);
    SET_FIELD(edma_ctxt->sirb_size, fwStrideSize);
    for (int i = 0; i < 8; i++)
    {
        auto compCfg = getCompCfg()[i].m_base;
        SET_FIELD(edma_ctxt->comp_cfg[i], compCfg);
    }

    PRINT_PACKET_TRACE(
        scalStream,
        "sched_arc_cmd_nic_edma_ops_t on GC_REDUCTION sched| command->opcode:{}, "
        " command->engine_group_type:{}, command->cmd_size:{} "
        "arc_cmd_update_edma_nic_ctxt_v3_t | opcode:{}, update_bitmap:{}, num_dwords:{} "
        "edma_nic_glbl_ctxt_v3_t | baseAddress[0]:0x{:x}, sibo_rank_stride[0]:{}, baseAddress[1]:0x{:x}, "
        "sibo_rank_stride[1]:{}, fwBaseAddress:0x{:x}, sirb_size:{}, "
        "comp_cfg: [0]:0x{:x}, [1]:0x{:x}, [2]:0x{:x}, [3]:0x{:x}, [4]:0x{:x}, [5]:0x{:x}, "
        "[6]:0x{:x}, [7]:0x{:x}",
        command->opcode,
        command->engine_group_type,
        command->cmd_size,
        edma_ops->opcode,
        edma_ops->update_bitmap,
        edma_ops->num_dwords,
        ((struct g3fw::edma_nic_glbl_ctxt_v3_t*)(command->edma_ctxt_v3->data))->sib_base_addr[0],
        ((struct g3fw::edma_nic_glbl_ctxt_v3_t*)(command->edma_ctxt_v3->data))->sibo_rank_stride[0],
        ((struct g3fw::edma_nic_glbl_ctxt_v3_t*)(command->edma_ctxt_v3->data))->sib_base_addr[1],
        ((struct g3fw::edma_nic_glbl_ctxt_v3_t*)(command->edma_ctxt_v3->data))->sibo_rank_stride[1],
        ((struct g3fw::edma_nic_glbl_ctxt_v3_t*)(command->edma_ctxt_v3->data))->sirb_base_addr,
        ((struct g3fw::edma_nic_glbl_ctxt_v3_t*)(command->edma_ctxt_v3->data))->sirb_size,
        ((struct g3fw::edma_nic_glbl_ctxt_v3_t*)(command->edma_ctxt_v3->data))->comp_cfg[0],
        ((struct g3fw::edma_nic_glbl_ctxt_v3_t*)(command->edma_ctxt_v3->data))->comp_cfg[1],
        ((struct g3fw::edma_nic_glbl_ctxt_v3_t*)(command->edma_ctxt_v3->data))->comp_cfg[2],
        ((struct g3fw::edma_nic_glbl_ctxt_v3_t*)(command->edma_ctxt_v3->data))->comp_cfg[3],
        ((struct g3fw::edma_nic_glbl_ctxt_v3_t*)(command->edma_ctxt_v3->data))->comp_cfg[4],
        ((struct g3fw::edma_nic_glbl_ctxt_v3_t*)(command->edma_ctxt_v3->data))->comp_cfg[5],
        ((struct g3fw::edma_nic_glbl_ctxt_v3_t*)(command->edma_ctxt_v3->data))->comp_cfg[6],
        ((struct g3fw::edma_nic_glbl_ctxt_v3_t*)(command->edma_ctxt_v3->data))->comp_cfg[7]);
}

void serializeUpdateNicOffsets(hcl::ScalStreamBase&                     scalStream,
                               bool                                     isSend,
                               bool                                     isScaleUp,
                               uint32_t                                 qpn,
                               std::array<uint16_t, MAX_NICS_GEN2ARCH>& remoteIndices)
{
    // calc the command size
    size_t size =
        (sizeof(g3fw::sched_arc_cmd_nic_passthrough_v2_t) + sizeof(gaudi3::Nic::direct_coll_desc_update_dest_rank));
    g3fw::sched_arc_cmd_nic_passthrough_v2_t* command =
        reinterpret_cast<g3fw::sched_arc_cmd_nic_passthrough_v2_t*>(scalStream.getNextPtr(size));
    memset(command, 0, size);

    auto opCode = getOpCode(isSend, isScaleUp);
    SET_FIELD(command->opcode, opCode);
    auto engineGroupType = getEngineGroupType(isSend, isScaleUp);
    SET_FIELD(command->engine_group_type, engineGroupType);
    SET_FIELD(command->num_dwords, sizeof(gaudi3::Nic::direct_coll_desc_update_dest_rank) >> 2);

    gaudi3::Nic::direct_coll_desc_update_dest_rank* desc =
        (gaudi3::Nic::direct_coll_desc_update_dest_rank*)command->passthrough_data;

    SET_FIELD(desc->fields.ctrl.qp, qpn);
    SET_FIELD(desc->fields.ctrl.cmd, gaudi3::Nic::COLL_CMD_DEST_RANK_UPDATE)

    memcpy(&desc->fields.nics[0], &remoteIndices[0], sizeof(desc->fields.nics));

    for (const struct gaudi3::Nic::direct_coll_desc_ent_dest_rank& nic : desc->fields.nics)
    {
        LOG_TRACE(HCL, "{}:: qpn={}, nic_lane_0={}, nic_lane_2={}", __FUNCTION__, qpn, nic.nic_lane_0, nic.nic_lane_2);
    }
}

void serializeUpdateLastRank(hcl::ScalStreamBase& scalStream,
                             bool                 isSend,
                             bool                 isScaleUp,
                             uint32_t             qpn,
                             uint32_t             ports_mask)
{
    // calc the command size
    size_t size =
        (sizeof(g3fw::sched_arc_cmd_nic_passthrough_v2_t) + sizeof(gaudi3::Nic::direct_coll_desc_update_last_rank));
    g3fw::sched_arc_cmd_nic_passthrough_v2_t* command =
        reinterpret_cast<g3fw::sched_arc_cmd_nic_passthrough_v2_t*>(scalStream.getNextPtr(size));
    memset(command, 0, size);

    // fill in sched_arc_cmd_nic_passthrough_v2_t
    auto opCode = getOpCode(isSend, isScaleUp);
    SET_FIELD(command->opcode, opCode);
    auto engineGroupType = getEngineGroupType(isSend, isScaleUp);
    SET_FIELD(command->engine_group_type, engineGroupType);
    SET_FIELD(command->num_dwords, sizeof(gaudi3::Nic::direct_coll_desc_update_last_rank) / 4);

    gaudi3::Nic::direct_coll_desc_update_last_rank* desc =
        (gaudi3::Nic::direct_coll_desc_update_last_rank*)command->passthrough_data;

    SET_FIELD(desc->fields.ctrl.qp, qpn);
    SET_FIELD(desc->fields.ctrl.cmd, gaudi3::Nic::COLL_CMD_LAST_RANK_UPDATE);
    SET_FIELD(desc->fields.p_0_23.ports, ports_mask & 0xFFFFFF);
    LOG_TRACE(HCL,
              "{}:: desc->fields.ctrl.qp={}, desc->fields.ctrl.cmd={}, desc->fields.p_0_23.ports={:b}",
              __FUNCTION__,
              desc->fields.ctrl.qp,
              desc->fields.ctrl.cmd,
              desc->fields.p_0_23.ports);
}

void serializeNopCommand(hcl::ScalStreamBase& scalStream, unsigned schedIdx, uint32_t padding)
{
    g3fw::sched_arc_cmd_nop_t* command =
        reinterpret_cast<g3fw::sched_arc_cmd_nop_t*>(scalStream.getNextPtr(sizeof(g3fw::sched_arc_cmd_nop_t)));
    memset(command, 0, sizeof(g3fw::sched_arc_cmd_nop_t));

    static const unsigned opcodes[(unsigned)hcl::SchedulersIndex::count] = {g3fw::SCHED_GC_REDUCTION_ARC_CMD_NOP,
                                                                            g3fw::SCHED_SCALEUP_SEND_ARC_CMD_NOP,
                                                                            g3fw::SCHED_SCALEUP_RECV_ARC_CMD_NOP,
                                                                            g3fw::SCHED_SCALEOUT_SEND_ARC_CMD_NOP,
                                                                            g3fw::SCHED_SCALEOUT_RECV_ARC_CMD_NOP};
    SET_FIELD(command->opcode, opcodes[schedIdx]);
    SET_FIELD(command->padding_count, (uint32_t)((padding - sizeof(g3fw::sched_arc_cmd_nop_t)) / sizeof(uint32_t)));
    PRINT_PACKET_TRACE(scalStream, "schedIdx:{}, command->padding_count:{}", schedIdx, command->padding_count);
}

void serializeAllocBarrierCommand(hcl::ScalStreamBase&                                     scalStream,
                                  unsigned                                                 schedIdx,
                                  uint32_t                                                 completionGroupIndex,
                                  uint32_t                                                 requiredSobs,
                                  llvm_vecsmall::SmallVector<uint32_t, MAX_STREAM_TO_INC>* fences,
                                  const LBWBurstData_t*                                    destBurstData)
{
    uint32_t fenceCnt    = fences == nullptr ? 0 : fences->size();
    uint32_t lbwBurstCnt = destBurstData == nullptr ? 0 : destBurstData->size();
    uint32_t fenceDwords = (fenceCnt > 0) + (fenceCnt > 4);

    uint32_t cmdSize = sizeof(g3fw::sched_arc_cmd_alloc_nic_barrier_t) +
                       (sizeof(g3fw::sched_arc_fence_id_arr_t) * fenceDwords) +
                       (sizeof(g3fw::sched_arc_lbw_write_t) * lbwBurstCnt);

    g3fw::sched_arc_cmd_alloc_nic_barrier_t* command =
        reinterpret_cast<g3fw::sched_arc_cmd_alloc_nic_barrier_t*>(scalStream.getNextPtr(cmdSize));
    memset(command, 0, cmdSize);

    static const unsigned opcodes[(unsigned)hcl::SchedulersIndex::count] = {
        g3fw::SCHED_GC_REDUCTION_ARC_CMD_ALLOC_NIC_BARRIER,
        g3fw::SCHED_SCALEUP_SEND_ARC_CMD_ALLOC_NIC_BARRIER,
        g3fw::SCHED_SCALEUP_RECV_ARC_CMD_ALLOC_NIC_BARRIER,
        g3fw::SCHED_SCALEOUT_SEND_ARC_CMD_ALLOC_NIC_BARRIER,
        g3fw::SCHED_SCALEOUT_RECV_ARC_CMD_ALLOC_NIC_BARRIER};
    SET_FIELD(command->opcode, opcodes[schedIdx]);
    SET_FIELD(command->comp_group_index, completionGroupIndex);
    SET_FIELD(command->required_sobs, requiredSobs);

    SET_FIELD(command->cmd_size_dwords, cmdSize >> 2);
    SET_FIELD(command->fence_count, fenceCnt);

    uint8_t* fence_arr_ptr = (uint8_t*)command + sizeof(g3fw::sched_arc_cmd_alloc_nic_barrier_t);
    for (unsigned i = 0; i < fenceCnt; i++)
    {
        SET_FIELD(fence_arr_ptr[i], (*fences)[i]);
    }

    SET_FIELD(command->num_lbw_write, lbwBurstCnt);
    g3fw::sched_arc_lbw_write_t* lbw_addr_data = (g3fw::sched_arc_lbw_write_t*)(command->fence_arr + fenceDwords);
    for (unsigned i = 0; i < lbwBurstCnt; i++)
    {
        SET_FIELD(lbw_addr_data[i].addr, (*destBurstData)[i].addr);
        SET_FIELD(lbw_addr_data[i].data, (*destBurstData)[i].data);
    }

    PRINT_PACKET_TRACE_WITH_2_COUNTS(scalStream,
                                     fenceCnt,
                                     lbwBurstCnt,
                                     "schedIdx:{}, opcode:{}, comp_group_index:{}, required_sobs:{}",
                                     schedIdx,
                                     command->opcode,
                                     (uint32_t)command->comp_group_index,
                                     (uint32_t)command->required_sobs);

    for (unsigned i = 0; i < fenceCnt; i++)
    {
        LOG_TRACE(HCL_SUBMIT, "Packets | fenceId{}={}", i, (*fences)[i]);
    }
    for (unsigned i = 0; i < lbwBurstCnt; i++)
    {
        LOG_TRACE(HCL_SUBMIT,
                  "Packets | BurstElement burst_size:{}, "
                  "burst_index:{}, "
                  "dst_addr:0x{:x}, src_data:0x{:x}",
                  (uint32_t)command->num_lbw_write,
                  (uint32_t)i,
                  (uint64_t)lbw_addr_data[i].addr,
                  (uint64_t)lbw_addr_data[i].data);
    }
}

void serializeFenceDecCommand(hcl::ScalStreamBase& scalStream, unsigned schedIdx, uint32_t fenceIndex, uint32_t target)
{
    g3fw::sched_arc_cmd_acp_fence_wait_t* command = reinterpret_cast<g3fw::sched_arc_cmd_acp_fence_wait_t*>(
        scalStream.getNextPtr(sizeof(g3fw::sched_arc_cmd_acp_fence_wait_t)));
    memset(command, 0, sizeof(g3fw::sched_arc_cmd_acp_fence_wait_t));

    static const unsigned opcodes[(unsigned)hcl::SchedulersIndex::count] = {
        g3fw::SCHED_GC_REDUCTION_ARC_CMD_ACP_FENCE_WAIT,
        g3fw::SCHED_SCALEUP_SEND_ARC_CMD_ACP_FENCE_WAIT,
        g3fw::SCHED_SCALEUP_RECV_ARC_CMD_ACP_FENCE_WAIT,
        g3fw::SCHED_SCALEOUT_SEND_ARC_CMD_ACP_FENCE_WAIT,
        g3fw::SCHED_SCALEOUT_RECV_ARC_CMD_ACP_FENCE_WAIT};

    SET_FIELD(command->opcode, opcodes[schedIdx]);
    SET_FIELD(command->fence_id, fenceIndex);
    SET_FIELD(command->target, target);
    PRINT_PACKET_TRACE(scalStream,
                       "sched: {}, opcode:{} , target:{}, fence_id:{}",
                       schedIdx,
                       command->opcode,
                       (uint32_t)command->target,
                       (uint32_t)command->fence_id);
}

void serializeFenceIncCommand(hcl::ScalStreamBase&      scalStream,
                              unsigned                  schedIdx,
                              uint32_t                  fenceIndex,
                              [[maybe_unused]] uint32_t target)
{
    g3fw::sched_arc_cmd_acp_fence_inc_immediate_t* command =
        reinterpret_cast<g3fw::sched_arc_cmd_acp_fence_inc_immediate_t*>(
            scalStream.getNextPtr(sizeof(g3fw::sched_arc_cmd_acp_fence_inc_immediate_t)));
    memset(command, 0, sizeof(g3fw::sched_arc_cmd_acp_fence_inc_immediate_t));

    static const unsigned opcodes[(unsigned)hcl::SchedulersIndex::count] = {
        g3fw::SCHED_GC_REDUCTION_ARC_CMD_ACP_FENCE_INC_IMMEDIATE,
        g3fw::SCHED_SCALEUP_SEND_ARC_CMD_ACP_FENCE_INC_IMMEDIATE,
        g3fw::SCHED_SCALEUP_RECV_ARC_CMD_ACP_FENCE_INC_IMMEDIATE,
        g3fw::SCHED_SCALEOUT_SEND_ARC_CMD_ACP_FENCE_INC_IMMEDIATE,
        g3fw::SCHED_SCALEOUT_RECV_ARC_CMD_ACP_FENCE_INC_IMMEDIATE};
    SET_FIELD(command->opcode, opcodes[schedIdx]);
    SET_FIELD(command->value, 1);
    SET_FIELD(command->fence_id, fenceIndex);
    PRINT_PACKET_TRACE(scalStream,
                       "schedIdx:{}, opcode:{} , value:{}, fence_id:{}",
                       schedIdx,
                       command->opcode,
                       (uint32_t)command->value,
                       (uint32_t)command->fence_id);
}

void serializeLbwWriteCommand(hcl::ScalStreamBase& scalStream, unsigned schedIdx, uint32_t destination, uint32_t data)
{
    g3fw::sched_arc_cmd_lbw_write_t* command = reinterpret_cast<g3fw::sched_arc_cmd_lbw_write_t*>(
        scalStream.getNextPtr(sizeof(g3fw::sched_arc_cmd_lbw_write_t)));
    memset(command, 0, sizeof(g3fw::sched_arc_cmd_lbw_write_t));

    static const unsigned opcodes[(unsigned)hcl::SchedulersIndex::count] = {
        g3fw::SCHED_GC_REDUCTION_ARC_CMD_LBW_WRITE,
        g3fw::SCHED_SCALEUP_SEND_ARC_CMD_LBW_WRITE,
        g3fw::SCHED_SCALEUP_RECV_ARC_CMD_LBW_WRITE,
        g3fw::SCHED_SCALEOUT_SEND_ARC_CMD_LBW_WRITE,
        g3fw::SCHED_SCALEOUT_RECV_ARC_CMD_LBW_WRITE};
    SET_FIELD(command->opcode, opcodes[schedIdx]);
    SET_FIELD(command->dst_addr, destination);
    SET_FIELD(command->src_data, data);
    PRINT_PACKET_TRACE(scalStream,
                       "schedIdx:{}, command->opcode:{}, "
                       " command->dst_addr:0x{:x}, command->src_data:0x{:x}",
                       schedIdx,
                       command->opcode,
                       (uint64_t)command->dst_addr,
                       (uint64_t)command->src_data);
}

void serializeLbwWriteWithFenceDecCommand(hcl::ScalStreamBase& scalStream,
                                          unsigned             schedIdx,
                                          uint32_t             destination,
                                          uint32_t             data,
                                          uint32_t             fenceIndex,
                                          uint32_t             fenceTarget)
{
    g3fw::sched_arc_cmd_lbw_write_t* command = reinterpret_cast<g3fw::sched_arc_cmd_lbw_write_t*>(
        scalStream.getNextPtr(sizeof(g3fw::sched_arc_cmd_lbw_write_t)));
    memset(command, 0, sizeof(g3fw::sched_arc_cmd_lbw_write_t));

    static const unsigned opcodes[(unsigned)hcl::SchedulersIndex::count] = {
        g3fw::SCHED_GC_REDUCTION_ARC_CMD_LBW_WRITE,
        g3fw::SCHED_SCALEUP_SEND_ARC_CMD_LBW_WRITE,
        g3fw::SCHED_SCALEUP_RECV_ARC_CMD_LBW_WRITE,
        g3fw::SCHED_SCALEOUT_SEND_ARC_CMD_LBW_WRITE,
        g3fw::SCHED_SCALEOUT_RECV_ARC_CMD_LBW_WRITE};
    SET_FIELD(command->opcode, opcodes[schedIdx]);
    SET_FIELD(command->dst_addr, destination);
    SET_FIELD(command->src_data, data);
    SET_FIELD(command->fence, 1);
    SET_FIELD(command->fence_id, fenceIndex);
    SET_FIELD(command->target, fenceTarget);

    PRINT_PACKET_TRACE(scalStream,
                       "schedIdx:{}, opcode:{} , dst_addr:0x{:x}, "
                       "src_data:0x{:x}, fence decrement id:{} to target:{}",
                       schedIdx,
                       command->opcode,
                       (uint64_t)command->dst_addr,
                       (uint64_t)command->src_data,
                       (uint32_t)command->fence_id,
                       (uint32_t)command->target);
}

void serializeLbwBurstWriteCommand(hcl::ScalStreamBase& scalStream, unsigned schedIdx, const LBWBurstData_t& destData)
{
    VERIFY((destData.size() != 0), "vector size [{}] for {}", destData.size(), __func__);
    size_t size = sizeof(g3fw::sched_arc_cmd_lbw_burst_write_t) + (sizeof(LbwData) * destData.size());
    g3fw::sched_arc_cmd_lbw_burst_write_t* command =
        reinterpret_cast<g3fw::sched_arc_cmd_lbw_burst_write_t*>(scalStream.getNextPtr(size));
    memset(command, 0, size);

    static const unsigned opcodes[(unsigned)hcl::SchedulersIndex::count] = {
        g3fw::SCHED_GC_REDUCTION_ARC_CMD_LBW_BURST_WRITE,
        g3fw::SCHED_SCALEUP_SEND_ARC_CMD_LBW_BURST_WRITE,
        g3fw::SCHED_SCALEUP_RECV_ARC_CMD_LBW_BURST_WRITE,
        g3fw::SCHED_SCALEOUT_SEND_ARC_CMD_LBW_BURST_WRITE,
        g3fw::SCHED_SCALEOUT_RECV_ARC_CMD_LBW_BURST_WRITE};
    SET_FIELD(command->opcode, opcodes[schedIdx]);
    SET_FIELD(command->num_lbw_write, destData.size());

    PRINT_PACKET_TRACE_WITH_COUNTS(scalStream, destData.size(), "");

    for (unsigned i = 0; i < destData.size(); i++)
    {
        SET_FIELD(command->addr_data[i].addr, destData[i].addr);
        SET_FIELD(command->addr_data[i].data, destData[i].data);
        LOG_TRACE(HCL_SUBMIT,
                  "Packets | BurstElement schedIdx:{}, opcode:{} , burst_size:{}, "
                  "burst_index:{}, "
                  "dst_addr:0x{:x}, src_data:0x{:x}",
                  schedIdx,
                  command->opcode,
                  (uint32_t)command->num_lbw_write,
                  (uint32_t)i,
                  (uint64_t)command->addr_data[i].addr,
                  (uint64_t)command->addr_data[i].data);
    }
}

void serializeDmaCommand(hcl::ScalStreamBase&            scalStream,
                         unsigned                        schedIdx,
                         uint32_t                        dmaType,
                         uint32_t                        soAddressLSB,
                         uint32_t                        size,
                         uint64_t                        destAddress,
                         uint64_t                        srcAddress,
                         hcclRedOp_t                     reduceOp,
                         uint8_t                         streamCtxtID,
                         [[maybe_unused]] hcclDataType_t dataType,
                         uint32_t                        poolId,
                         bool                            isForScaleout,
                         bool                            useCasting,
                         uint32_t                        numberOfRanks,
                         uint32_t                        numberOfSubBuffers,
                         uint32_t                        indexOfSubBuffer,
                         bool                            is16BitMemcpy,
                         uint32_t                        secondSoAddress,
                         bool                            isBFloat,
                         bool                            useReductionInd,
                         bool                            isFirstWrite,
                         uint32_t                        memsetValue)
{
    size_t sizeInBytes = sizeof(g3fw::sched_arc_cmd_nic_edma_ops_t);
    switch (dmaType)
    {
        case g3fw::NIC_EDMA_CMD_SIBO_OPS_V3:
            sizeInBytes += sizeof(g3fw::arc_cmd_nic_edma_sibo_ops_v3_t);
            break;
        case g3fw::NIC_EDMA_CMD_LIN_OPS_V3:
            sizeInBytes += sizeof(g3fw::arc_cmd_nic_edma_lin_ops_v3_t);
            break;
        case g3fw::NIC_EDMA_CMD_LIN_MEMSET_V3_2:
            sizeInBytes += sizeof(g3fw::arc_cmd_nic_edma_lin_memset_v3_2_t);
            break;
        default:
            VERIFY(sizeInBytes != sizeof(g3fw::sched_arc_cmd_nic_edma_ops_t),
                   "unsupported dmaType [{}] for {}",
                   dmaType,
                   __func__);
    }

    g3fw::sched_arc_cmd_nic_edma_ops_t* command =
        reinterpret_cast<g3fw::sched_arc_cmd_nic_edma_ops_t*>(scalStream.getNextPtr(sizeInBytes));
    memset(command, 0, sizeInBytes);

    static const unsigned opcodes[(unsigned)hcl::SchedulersIndex::count] = {
        g3fw::SCHED_GC_REDUCTION_ARC_CMD_NIC_EDMA_OPS,
        g3fw::SCHED_SCALEUP_SEND_ARC_CMD_NIC_EDMA_OPS,
    };

    static const unsigned groupEngine[(unsigned)hcl::SchedulersIndex::count][g3fw::NIC_EDMA_COUNT] = {
        {SCAL_EDMA_NETWORK_GC_REDUCTION_GROUP0,
         SCAL_EDMA_NETWORK_GC_REDUCTION_GROUP0,
         0,
         SCAL_EDMA_NETWORK_GC_REDUCTION_GROUP0,  // dma scheduler
         0,
         0,
         SCAL_EDMA_NETWORK_GC_REDUCTION_GROUP0},  // for scaleup_init command
        {SCAL_EDMA_NETWORK_SCALE_UP_SEND_GROUP0,
         SCAL_EDMA_NETWORK_SCALE_UP_SEND_GROUP0,
         0,
         0},  // scaleup send scheduler
        {SCAL_EDMA_NETWORK_SCALE_UP_RECV_GROUP0,
         SCAL_EDMA_NETWORK_SCALE_UP_RECV_GROUP0,
         0,
         0},  // scaleup recv scheduler
        {0, 0, 0, 0},
        {0, 0, 0, 0}};

    command->opcode = opcodes[schedIdx];
    switch (dmaType)
    {
        case g3fw::NIC_EDMA_CMD_SIBO_OPS_V3:
            SET_FIELD(command->cmd_size, sizeof(g3fw::arc_cmd_nic_edma_sibo_ops_v3_t) + sizeof(uint32_t));
            break;
        case g3fw::NIC_EDMA_CMD_LIN_OPS_V3:
            SET_FIELD(command->cmd_size, sizeof(g3fw::arc_cmd_nic_edma_lin_ops_v3_t) + sizeof(uint32_t));
            break;
        case g3fw::NIC_EDMA_CMD_LIN_MEMSET_V3_2:
            SET_FIELD(command->cmd_size, sizeof(g3fw::arc_cmd_nic_edma_lin_memset_v3_2_t) + sizeof(uint32_t));
            break;
        default:
            VERIFY(command->cmd_size != 0, "unsupported dmaType [{}] for {}", dmaType, __func__);
    }
    command->engine_group_type = groupEngine[schedIdx][dmaType];

    VERIFY(command->engine_group_type != 0, "unsupported dmaType [{}] for {}", dmaType, __func__);

    if (dmaType == static_cast<uint32_t>(g3fw::NIC_EDMA_CMD_SIBO_OPS_V3))
    {
        auto                                         firstSoIdxBaseIdx  = getSoIdxBaseIdx(soAddressLSB);
        auto                                         secondSoIdxBaseIdx = getSoIdxBaseIdx(secondSoAddress);
        struct g3fw::arc_cmd_nic_edma_sibo_ops_v3_t* edma_ops =
            (struct g3fw::arc_cmd_nic_edma_sibo_ops_v3_t*)&command->sibo_ops_v3;
        reduction_operation_e reductionOp = getReductionOp(reduceOp);
        SET_FIELD(edma_ops->reduction_op, reductionOp);
        SET_FIELD(edma_ops->sibo_index, indexOfSubBuffer * numberOfSubBuffers);
        SET_FIELD(edma_ops->rank_count, numberOfRanks - 1);
        SET_FIELD(edma_ops->rank_offset_in_sibo, isForScaleout ? 1 : 0);
        SET_FIELD(edma_ops->pool_id, poolId);
        SET_FIELD(edma_ops->opcode, dmaType);
        SET_FIELD(edma_ops->transfer_size, size);
        SET_FIELD(edma_ops->dst_addr_lo, destAddress & 0xffffffff);
        SET_FIELD(edma_ops->signal_second, secondSoAddress != 0);
        SET_FIELD(edma_ops->sob_base, firstSoIdxBaseIdx.baseIdx & 0x7);
        SET_FIELD(edma_ops->sob_index, firstSoIdxBaseIdx.soIdx & 0x3ff);
        SET_FIELD(edma_ops->second_sob_base, secondSoIdxBaseIdx.baseIdx & 0x7);
        SET_FIELD(edma_ops->second_sob_index, secondSoIdxBaseIdx.soIdx & 0x3ff);
        SET_FIELD(edma_ops->dst_addr_hi, destAddress >> 32);
        SET_FIELD(edma_ops->src_addr_lo, srcAddress & 0xffffffff);
        SET_FIELD(edma_ops->src_addr_hi, (srcAddress >> 32) & 0xffffff);
        SET_FIELD(edma_ops->local_datasize, is16BitMemcpy ? 1 : 2);  // 16bit / 32bit
        SET_FIELD(edma_ops->sibo_datasize, is16BitMemcpy ? 1 : 2);   // 16bit / 32bit
        SET_FIELD(edma_ops->output_datasize,
                  ((is16BitMemcpy && !useCasting) || (!is16BitMemcpy && useCasting)) ? 1 : 2);  // 16bit / 32bit
        SET_FIELD(edma_ops->local_hbw_axcache, DEFAULT_CACHE_ALLOC);
        SET_FIELD(edma_ops->local_class_type, DEFAULT_CACHE_CLASS);
        SET_FIELD(edma_ops->output_hbw_axcache, DEFAULT_CACHE_ALLOC);
        SET_FIELD(edma_ops->output_class_type, DEFAULT_CACHE_CLASS);
        SET_FIELD(edma_ops->dtype, get_nic_edma_dtype(dataType, is16BitMemcpy, useCasting, isBFloat));  // BF / FP
        SET_FIELD(edma_ops->reduction_ind, (useReductionInd ? 1 : 0));
        SET_FIELD(edma_ops->context_id, streamCtxtID);

        PRINT_PACKET_TRACE(
            scalStream,
            "with arc_cmd_nic_edma_sibo_ops_v3_t. "
            "schedIdx:{}, Command[0-3]: 0x{:x}, 0x{:x}, 0x{:x}, command address: 0x{:x}, sched_opcode: {}, "
            "cmd_size:{}, engine_group_type:{}, engine: opcode:{}, sibo_index:{}, rank_offset_in_sibo:{}, "
            "rank_count:{}, signal_second:{}, "
            "sob_base:{}, sob_index:0x{:x}, (soAddressLSB:0x{:x}), "
            "second_sob_base:{}, second_sob_index:0x{:x}, (secondSoAddress:0x{:x}), "
            "transfer_size:{}, pool_id:{}, "
            "srcAddr:0x{:x}, dstAddr:0x{:x}, dst_addr_lo:0x{:x}, dst_addr_hi:0x{:x}, src_addr_lo:0x{:x}, "
            "src_addr_hi:0x{:x}, local_hbw_axcache:0x{:x}, local_class_type:0x{:x}"
            "reduction_ind:{}, reduction_op:{}, local_datasize:{}, sibo_datasize:{}, "
            "output_datasize:{}, dtype:{}",
            schedIdx,
            *((uint32_t*)(command)),
            *((uint32_t*)(command) + 1),
            *((uint32_t*)(command) + 2),
            (uint64_t)command,
            command->opcode,
            command->cmd_size,
            command->engine_group_type,
            (uint32_t)edma_ops->opcode,
            (uint32_t)edma_ops->sibo_index,
            (uint32_t)edma_ops->rank_offset_in_sibo,
            (uint32_t)edma_ops->rank_count,
            (bool)edma_ops->signal_second,
            (uint32_t)edma_ops->sob_base,
            (uint32_t)edma_ops->sob_index,
            (uint32_t)soAddressLSB,
            (uint32_t)edma_ops->second_sob_base,
            (uint32_t)edma_ops->second_sob_index,
            (uint32_t)secondSoAddress,
            (uint32_t)edma_ops->transfer_size,
            (uint32_t)edma_ops->pool_id,
            (uint64_t)srcAddress,
            (uint64_t)destAddress,
            (uint64_t)edma_ops->dst_addr_lo,
            (uint64_t)edma_ops->dst_addr_hi,
            (uint64_t)edma_ops->src_addr_lo,
            (uint64_t)edma_ops->src_addr_hi,
            (uint64_t)edma_ops->local_hbw_axcache,
            (uint64_t)edma_ops->local_class_type,
            (uint32_t)edma_ops->reduction_ind,
            (uint32_t)edma_ops->reduction_op,
            (uint32_t)edma_ops->local_datasize,
            (uint32_t)edma_ops->sibo_datasize,
            (uint32_t)edma_ops->output_datasize,
            (uint32_t)edma_ops->dtype);
    }
    else if (dmaType == static_cast<uint32_t>(g3fw::NIC_EDMA_CMD_LIN_OPS_V3))
    {
        struct g3fw::arc_cmd_nic_edma_lin_ops_v3_t* edma_ops =
            (struct g3fw::arc_cmd_nic_edma_lin_ops_v3_t*)&command->lin_ops_v3;
        reduction_operation_e reductionOp =
            (is16BitMemcpy && useCasting && isFirstWrite) ? REDUCTION_OP_NONE : getReductionOp(reduceOp);
        SET_FIELD(edma_ops->reduction_op, reductionOp);
        SET_FIELD(edma_ops->sob_address, soAddressLSB & 0x7ffffff);
        SET_FIELD(edma_ops->opcode, dmaType);
        SET_FIELD(edma_ops->hbw_axcache, (DEFAULT_CACHE_ALLOC << 4) + DEFAULT_CACHE_ALLOC);  // WR + RD
        SET_FIELD(edma_ops->class_type, (DEFAULT_CACHE_CLASS << 4) + DEFAULT_CACHE_CLASS);   // RD + WR
        SET_FIELD(edma_ops->transfer_size, size);
        SET_FIELD(edma_ops->dst_addr_lo, destAddress & 0xffffffff);
        SET_FIELD(edma_ops->dst_addr_hi, destAddress >> 32);
        SET_FIELD(edma_ops->src_addr_lo, srcAddress & 0xffffffff);
        SET_FIELD(edma_ops->src_addr_hi, (srcAddress >> 32) & 0xffffff);
        SET_FIELD(edma_ops->input_datasize, is16BitMemcpy ? 1 : 2);  // 16bit / 32bit
        SET_FIELD(edma_ops->output_datasize,
                  (is16BitMemcpy && !useCasting) || (!is16BitMemcpy && useCasting) ? 1 : 2);  // 16bit / 32bit
        SET_FIELD(edma_ops->dtype, ((is16BitMemcpy || useCasting) && isBFloat) ? 3 : 2);      // BF / FP
        SET_FIELD(edma_ops->reduction_ind, useReductionInd ? 1 : 0);
        SET_FIELD(edma_ops->context_id, streamCtxtID);

        PRINT_PACKET_TRACE(
            scalStream,
            "with arc_cmd_nic_edma_lin_ops_v3_t. "
            "schedIdx:{}, Command[0-3]: 0x{:x}, 0x{:x}, 0x{:x}, command address: 0x{:x}, sched_opcode: {}, "
            "cmd_size:{}, engine_group_type:{}, engine: opcode:{}, sob_address:0x{:x}, "
            "transfer_size:{}, srcAddr:0x{:x}, dstAddr:0x{:x}, dst_addr_lo:0x{:x}, dst_addr_hi:0x{:x}, "
            "src_addr_lo:0x{:x}, src_addr_hi:0x{:x}, reduction_ind:{}, reduction_op:{}, input_datasize:{}, "
            "output_datasize:{}, dtype:0x{:x}, hbw_axcache:0x{:x}, class_type:0x{:x}",
            schedIdx,
            *((uint32_t*)(command)),
            *((uint32_t*)(command) + 1),
            *((uint32_t*)(command) + 2),
            (uint64_t)command,
            command->opcode,
            command->cmd_size,
            command->engine_group_type,
            (uint32_t)edma_ops->opcode,
            (uint64_t)edma_ops->sob_address,
            (uint32_t)edma_ops->transfer_size,
            (uint64_t)srcAddress,
            (uint64_t)destAddress,
            (uint64_t)edma_ops->dst_addr_lo,
            (uint64_t)edma_ops->dst_addr_hi,
            (uint64_t)edma_ops->src_addr_lo,
            (uint64_t)edma_ops->src_addr_hi,
            (uint32_t)edma_ops->reduction_ind,
            (uint32_t)edma_ops->reduction_op,
            (uint32_t)edma_ops->input_datasize,
            (uint32_t)edma_ops->output_datasize,
            (uint32_t)edma_ops->dtype,
            (uint32_t)edma_ops->hbw_axcache,
            (uint32_t)edma_ops->class_type);
    }
    else  // (dmaType == static_cast<uint32_t>(g3fw::NIC_EDMA_CMD_LIN_MEMSET_V3_2))
    {
        const auto                                       firstSoIdxBaseIdx = getSoIdxBaseIdx(soAddressLSB);
        struct g3fw::arc_cmd_nic_edma_lin_memset_v3_2_t* edma_ops =
            (struct g3fw::arc_cmd_nic_edma_lin_memset_v3_2_t*)&command->lin_memset;
        const auto comp_cfg = getCompCfg();
        SET_FIELD(edma_ops->sob_base, firstSoIdxBaseIdx.baseIdx & 0x7);
        SET_FIELD(edma_ops->sob_index, firstSoIdxBaseIdx.soIdx & 0x3ff);
        SET_FIELD(edma_ops->opcode, dmaType);
        SET_FIELD(edma_ops->hbw_axcache, DEFAULT_CACHE_ALLOC);
        SET_FIELD(edma_ops->class_type, DEFAULT_CACHE_CLASS);
        SET_FIELD(edma_ops->transfer_size, size);
        SET_FIELD(edma_ops->dst_addr_lo, destAddress & 0xffffffff);
        SET_FIELD(edma_ops->dst_addr_hi, destAddress >> 32);
        SET_FIELD(edma_ops->context_id, streamCtxtID);
        SET_FIELD(edma_ops->memset_value, memsetValue);

        PRINT_PACKET_TRACE(
            scalStream,
            "with arc_cmd_nic_edma_lin_memset_v3_2_t. "
            "schedIdx:{}, Command[0-3]: 0x{:x}, 0x{:x}, 0x{:x}, command address: 0x{:x}, sched_opcode: {}, "
            "cmd_size:{}, engine_group_type:{}, engine: opcode:{}, sob_address:0x{:x}, sob_base:{}, sob_index:{} "
            "transfer_size:{}, dstAddr:0x{:x}, dst_addr_lo:0x{:x}, dst_addr_hi:0x{:x}",
            schedIdx,
            *((uint32_t*)(command)),
            *((uint32_t*)(command) + 1),
            *((uint32_t*)(command) + 2),
            (uint64_t)command,
            command->opcode,
            command->cmd_size,
            command->engine_group_type,
            (uint32_t)edma_ops->opcode,
            (uint64_t)comp_cfg[edma_ops->sob_base].m_base + (uint64_t)edma_ops->sob_index * 4,
            (uint64_t)edma_ops->sob_base,
            (uint64_t)edma_ops->sob_index,
            (uint32_t)edma_ops->transfer_size,
            (uint64_t)destAddress,
            (uint64_t)edma_ops->dst_addr_lo,
            (uint64_t)edma_ops->dst_addr_hi);
    }
}

void serializePdmaCommand(hcl::ScalStreamBase&            scalStream,
                          unsigned                        schedIdx,
                          bool                            isDownload,
                          uint64_t                        hostAddress,
                          uint64_t                        deviceAddress,
                          uint32_t                        size,
                          bool                            isReduction,
                          hcclRedOp_t                     reduceOp,
                          bool                            isCastUp,
                          uint8_t                         apiId,
                          [[maybe_unused]] unsigned       streamIndex,
                          uint8_t                         streamCtxtID,
                          [[maybe_unused]] hcclDataType_t dataType,
                          uint32_t                        sobAddr)
{
    static const unsigned opcodes[(unsigned)hcl::SchedulersIndex::count] = {
        g3fw::SCHED_GC_REDUCTION_ARC_CMD_COUNT,
        g3fw::SCHED_SCALEUP_SEND_ARC_CMD_COUNT,
        g3fw::SCHED_SCALEUP_RECV_ARC_CMD_COUNT,
        g3fw::SCHED_SCALEOUT_SEND_ARC_CMD_PDMA_BATCH_TRANSFER,
        g3fw::SCHED_SCALEOUT_RECV_ARC_CMD_PDMA_BATCH_TRANSFER};

    uint8_t batchCount = 1;  // HCL use only single transfer mode
    size_t  cmdSize =
        sizeof(g3fw::sched_arc_cmd_pdma_batch_transfer_t) + batchCount * sizeof(g3fw::sched_arc_pdma_commands_params_t);

    g3fw::sched_arc_cmd_pdma_batch_transfer_t* command =
        reinterpret_cast<g3fw::sched_arc_cmd_pdma_batch_transfer_t*>(scalStream.getNextPtr(cmdSize));
    memset(command, 0, cmdSize);

    VERIFY(schedIdx == (unsigned)hcl::SchedulersIndex::sendScaleOut ||
           schedIdx == (unsigned)hcl::SchedulersIndex::recvScaleOut);

    if (isDownload)
    {
        SET_FIELD(command->engine_group_type, (unsigned)SCAL_PDMA_NETWORK_SCALE_OUT_RECV_GROUP);
        SET_FIELD(command->workload_type,
                  isCastUp ? g3fw::ENG_PDMA_ARC_CMD_BATCH_CASTUP : g3fw::ENG_PDMA_ARC_CMD_BATCH_TRANSFER);
        SET_FIELD(command->batch_params->src_addr, hostAddress);
        SET_FIELD(command->batch_params->dst_addr, deviceAddress);
    }
    else  // upload
    {
        VERIFY(!isCastUp, "upload cannot require cast up");
        SET_FIELD(command->engine_group_type, (unsigned)SCAL_PDMA_NETWORK_SCALE_OUT_SEND_GROUP);
        SET_FIELD(command->workload_type, g3fw::ENG_PDMA_ARC_CMD_BATCH_COMMANDS);
        SET_FIELD(command->batch_params->src_addr, deviceAddress);
        SET_FIELD(command->batch_params->dst_addr, hostAddress);
    }
    SET_FIELD(command->opcode, opcodes[schedIdx]);
    SET_FIELD(command->watch_dog_sig_value, 0);
    SET_FIELD(command->has_payload, 1);
    SET_FIELD(command->signal_to_cg, 0);
    SET_FIELD(command->reduction_ind, isReduction);
    reduction_operation_e reductionOp = isReduction ? getReductionOp(reduceOp) : REDUCTION_OP_NONE;
    SET_FIELD(command->reduction_op, reductionOp);
    SET_FIELD(command->reduction_dtype, isCastUp ? REDUCTION_UPSCALING_BF16 : REDUCTION_FP32);
    SET_FIELD(command->pay_data, 0x80000001);
    SET_FIELD(command->pay_addr, sobAddr);  // should also indicate 4 bit for cg index
    SET_FIELD(command->batch_params->transfer_size, size);
    SET_FIELD(command->batch_count, batchCount);
    SET_FIELD(command->api_id, apiId);
    SET_FIELD(command->stream_ctxt_id, streamCtxtID);

    if (command->has_payload)
    {
        VERIFY(!command->signal_to_cg, "both cannot be used at the same time");
    }

    PRINT_PACKET_TRACE(scalStream, "schedIdx:{}, apiID:{}", schedIdx, apiId);
}

}  // namespace SchedArcCommandsGaudi3
