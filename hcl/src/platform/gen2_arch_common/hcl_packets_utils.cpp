#include "hcl_packets_utils.h"
#include "platform/gen2_arch_common/hccl_device.h"
#include "hcl_global_conf.h"
#include "hcl_utils.h"
#include "platform/gen2_arch_common/types.h"  // for reduction_datatype_e
#include "define_synapse_common.hpp"          // for pdma context id
#include "synapse_profiler_api.hpp"           // for pdma context id
#include "internal/hcl_profiler_api.h"

SoIdxBaseIdx getSoIdxBaseIdx(uint32_t soAddress)
{
    SoIdxBaseIdx ret = SoIdxBaseIdx();
    if (0 == soAddress || !hccl_device().initialized) return ret;

    FOR_I(hccl_device()->getHal().getMaxArchStreams() * 2 + 1)
    {
        auto comp_cfg = getCompCfg();
        if (comp_cfg[i].m_base <= soAddress && soAddress < comp_cfg[i].m_base + comp_cfg[i].m_size * 4)
        {
            ret.baseIdx = i;
            ret.soIdx   = (soAddress - comp_cfg[i].m_base) >> 2;
            break;
        }
    }

    LOG_TRACE(HCL,
              "SO Address converted to comp_cfg terms (address: 0x{:x} => base index: {}, address index: 0x{:x})",
              soAddress,
              ret.baseIdx,
              ret.soIdx);

    if (likely(!GCFG_HCL_NULL_SUBMIT.value()))
    {
        VERIFY(ret.baseIdx != UINT32_MAX,
               "Could not translate SO address [0x{:x}] to base address index and SO index in comp_cfg",
               soAddress);
    }

    return ret;
}

SoBaseAndSize* getCompCfg()
{
    static SoBaseAndSize s_comp_cfg[8];
    return s_comp_cfg;
}

reduction_datatype_e getReductionDataType(bool isCastUp, hcclDataType_t dataType)
{
    reduction_datatype_e res = REDUCTION_FP32;

    switch (dataType)
    {
        case hcclBfloat16:
            res = isCastUp ? REDUCTION_UPSCALING_BF16 : REDUCTION_BF16;
            break;
        case hcclFloat16:
            res = isCastUp ? REDUCTION_UPSCALING_FP16 : REDUCTION_FP16;
            break;
        case hcclInt32:
            res = REDUCTION_INT32;
            break;
        case hcclUint32:
            res = REDUCTION_UINT32;
            break;
        default:
            break;
    }

    return res;
}

uint8_t getEdmaStreamCtxtId(uint8_t apiId, unsigned streamIndex)
{
    hcl::StreamContextEncoding streamCtxtID;

    // Ensure apiId and streamIndex are within the valid range
    streamCtxtID.api_id       = apiId & 0b11111;     // 5 bits
    streamCtxtID.stream_index = streamIndex & 0b11;  // 2 bits

    return streamCtxtID.raw;
}

uint8_t getEdmaDebugCtxtId(uint8_t apiId, uint8_t isScaleOut, uint8_t slice)
{
    hcl::StreamContextEncoding debugStreamCtxtID;

    debugStreamCtxtID.debug_api_id = apiId & 0b1111;    // 4 bits
    debugStreamCtxtID.is_scale_out = isScaleOut & 0b1;  // 1 bit
    debugStreamCtxtID.slice        = slice & 0b11;      // 2 bits

    return debugStreamCtxtID.raw;
}

uint8_t getPdmaStreamCtxtId(bool isDownload, unsigned streamIndex)
{
    PdmaDirCtx         direction  = isDownload ? PdmaDirCtx::DOWN : PdmaDirCtx::UP;
    internalStreamType streamType = internalStreamType::INTERNAL_STREAM_TYPE_COLLECTIVE_NETWORK;

    return (((((uint8_t)direction) & ContextEncoding::DIR_MASK) << ContextEncoding::DIR_OFFSET) |
            (((uint8_t)streamType) & ContextEncoding::TYPE_MASK) << ContextEncoding::TYPE_OFFSET) |
           ((((uint8_t)streamIndex) & ContextEncoding::STREAM_MASK) << ContextEncoding::STREAM_OFFSET);
}

nic_edma_datatypes_t get_nic_edma_dtype(hcclDataType_t dataType, bool is16BitMemcpy, bool useCasting, bool isBFloat)
{
    nic_edma_datatypes_t nic_edma_dtype = NIC_EDMA_FP;
    if ((is16BitMemcpy || useCasting) && isBFloat)
    {
        nic_edma_dtype = NIC_EDMA_BF;
    }
    if (dataType == hcclInt32)
    {
        nic_edma_dtype = NIC_EDMA_SIGNED;
    }
    else if (dataType == hcclUint32)
    {
        nic_edma_dtype = NIC_EDMA_UNSIGNED;
    }

    return nic_edma_dtype;
}