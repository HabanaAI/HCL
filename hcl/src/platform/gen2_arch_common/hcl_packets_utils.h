#pragma once

#include <cstdint>  // for uint32_t
#include <cstddef>  // for size_t

#include "platform/gen2_arch_common/types.h"  // for reduction_datatype_e
#include "hccl_types.h"                       // for hcclDataType_t

// Used for our command distribution tool, if the macro is changed then we also need to change the script
#define PRINT_PACKET_TRACE(scalStream, msg, ...)                                                                       \
    LOG_TRACE(HCL_SUBMIT, "Packets | {} " msg ", on stream:{}", __func__, ##__VA_ARGS__, *(scalStream.getStreamName()));
#define PRINT_PACKET_TRACE_WITH_COUNTS(scalStream, cnt, msg, ...)                                                      \
    LOG_TRACE(HCL_SUBMIT,                                                                                              \
              "Packets | {}({}) " msg ", on stream:{}",                                                                \
              __func__,                                                                                                \
              cnt,                                                                                                     \
              ##__VA_ARGS__,                                                                                           \
              *(scalStream.getStreamName()));

struct SoIdxBaseIdx
{
    uint32_t baseIdx = UINT32_MAX;
    uint32_t soIdx   = UINT32_MAX;
};

struct SoBaseAndSize
{
    uint32_t m_base;
    size_t   m_size;
    SoBaseAndSize() : m_base(0), m_size(0) {};
    SoBaseAndSize(uint32_t base, size_t size) : m_base(base), m_size(size) {};
};

SoIdxBaseIdx getSoIdxBaseIdx(uint32_t soAddress);

SoBaseAndSize* getCompCfg();

reduction_datatype_e getReductionDataType(bool isCastUp, hcclDataType_t dataType);

uint8_t getEdmaStreamCtxtId(uint8_t apiId, unsigned streamIndex);

uint8_t getEdmaDebugCtxtId(uint8_t apiId, uint8_t isScaleOut, uint8_t slice);

uint8_t getPdmaStreamCtxtId(bool isDownload, unsigned streamIndex);
