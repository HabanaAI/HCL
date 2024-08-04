#pragma once

#include <cstdint>

#define MAX_NICS_GEN2ARCH                 (24)
#define GEN2ARCH_HLS_BOX_SIZE             (8)
#define MAX_DYNAMIC_PORT_SCHEMES_GEN2ARCH (3)
#define HCL_INVALID_PORT                  (uint16_t)(-1)  // 0xFFFF

class QpInfo
{
public:
    QpInfo() : qpn(0), qpi(0) {}
    QpInfo(uint32_t _qpn, uint32_t _qpi) : qpn(_qpn), qpi(_qpi) {}

protected:
    uint32_t qpn;
    uint32_t qpi;  // QP index

public:
    inline uint32_t getQpn() { return qpn; };
    inline uint32_t getQpi() { return qpi; };
};

enum reduction_datatype_e
{
    REDUCTION_INT8           = 0x0,
    REDUCTION_INT16          = 0x1,
    REDUCTION_INT32          = 0x2,
    REDUCTION_UINT8          = 0x3,
    REDUCTION_UINT16         = 0x4,
    REDUCTION_UINT32         = 0x5,
    REDUCTION_BF16           = 0x6,
    REDUCTION_FP32           = 0x7,
    REDUCTION_FP16           = 0x8,
    REDUCTION_UPSCALING_FP16 = 0xC,
    REDUCTION_UPSCALING_BF16 = 0xD
};

enum reduction_rounding_mode_e
{
    REDUCTION_ROUND_HALF_TO_NEAREST_EVEN = 0x0,
    REDUCTION_ROUND_TO_ZERO              = 0x1,
    REDUCTION_ROUND_UP                   = 0x2,
    REDUCTION_ROUND_DOWN                 = 0x3
};

struct sob_info
{
    unsigned smIdx;
    unsigned dcore;
    uint8_t  ssm;
    uint32_t sobId;
};

struct SyncObjectDescriptor
{
    sob_info sob;
    unsigned value;
};

struct MessageAddrToData
{
    uint32_t addr;
    uint32_t data;
};

struct fence_info
{
    unsigned          index;
    MessageAddrToData lbw;
};

enum reduceOpMemsetValues
{
    eSumMemsetOp = 0,
    eMinMemsetOp = 1,  // Set to +Inf
    eMaxMemsetOp = 3   // Set to -Inf
};

#include <vector>
using QpsVector = std::vector<uint32_t>;

#include <array>
using box_devices_t = std::array<int, GEN2ARCH_HLS_BOX_SIZE>;
