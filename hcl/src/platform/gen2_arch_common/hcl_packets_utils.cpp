#include "hcl_packets_utils.h"

#include "hccl_device.h"
#include "hcl_global_conf.h"
#include "hcl_utils.h"
#include "platform/gaudi2/hcl_device.h"  // for IHclDevice
#include "platform/gen2_arch_common/types.h"  // for reduction_datatype_e

SoIdxBaseIdx getSoIdxBaseIdx(uint32_t soAddress)
{
    SoIdxBaseIdx ret;
    if (0 == soAddress) return ret;

    FOR_I(hccl_device()->getHal()->getMaxStreams() * 2)
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
              "SO Adress converted to comp_cfg terms (address: 0x{:x} => base index: {}, adress index: 0x{:x})",
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
        default:
            break;
    }

    return res;
}