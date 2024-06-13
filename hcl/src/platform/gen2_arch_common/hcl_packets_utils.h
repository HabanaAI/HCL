#pragma once

#include <cstdint>  // for uint32_t
#include <cstddef>  // for size_t

#include "platform/gen2_arch_common/types.h"  // for reduction_datatype_e
#include "hccl_types.h"                       // for hcclDataType_t

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