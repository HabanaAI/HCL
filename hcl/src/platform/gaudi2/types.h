#pragma once

#include <cstdint>
#include <memory>
#include <functional>

#include "platform/gen2_arch_common/types.h"
#include "hccl_types.h"  // for hcclRedOp_t

#define HLS2_BOX_SIZE   8

enum eDWords
{
    DW0            = 0,
    DW1            = 1,
    DW2            = 2,
    DW3            = 3,
    DW4            = 4,
    DW_COMM_QP     = 5,
    DW_REMOTE_RANK = 6,
    DW_NUM         = 7
};

enum reduction_operation_e
{
    REDUCTION_OP_ADDITION    = 0x0,
    REDUCTION_OP_SUBTRACTION = 0x1,
    REDUCTION_OP_MINIMUM     = 0x2,
    REDUCTION_OP_MAXIMUM     = 0x3
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
union edwords_t
{
    struct
    {
        bool DW0            : 1; // 0 - 0
        bool DW1            : 1; // 1 - 1
        bool DW2            : 1; // 2 - 2
        bool DW3            : 1; // 3 - 3
        bool DW4            : 1; // 4 - 4
        bool DW_COMM_QP     : 1; // 5 - 5
        bool DW_REMOTE_RANK : 1; // 6 - 6
    };
    uint64_t raw = 0;
    operator uint64_t() {return raw;}
} __attribute__((packed));

union g2_nic_engine_reduction_opcode_t  // sizeof() == 16 bits
{
    struct
    {
        uint32_t                       indication : 1;  // When set to 1, reduction will be executed
        enum reduction_datatype_e      datatype : 4;
        enum reduction_operation_e     operation : 2;
        enum reduction_rounding_mode_e rounding : 2;
        uint32_t                       operation_AxUSER : 1;  // When set, this overrides the AxUSER[19:18]
        uint32_t                       pad : 6;
    };
    uint16_t raw;
} __attribute__((packed));
