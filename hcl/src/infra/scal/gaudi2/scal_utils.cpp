#include "infra/scal/gaudi2/scal_utils.h"
#include "hcl_utils.h"

#include "gaudi2/asic_reg_structs/sob_objs_regs.h"
#include "gaudi2/asic_reg/gaudi2_blocks.h"
#include "gaudi2_arc_host_packets.h"  // for gaudi2 FW COMP_SYNC_GROUP_CMAX_TARGET

uint64_t hcl::Gaudi2HclScalUtils::calculateSoAddressFromIdxAndSM(unsigned smIdx, unsigned idx)
{
    uint64_t smBase = 0;
    switch (smIdx)
    {
        case 0:
            smBase = mmDCORE0_SYNC_MNGR_OBJS_BASE;
            break;
        case 1:
            smBase = mmDCORE1_SYNC_MNGR_OBJS_BASE;
            break;
        case 2:
            smBase = mmDCORE2_SYNC_MNGR_OBJS_BASE;
            break;
        case 3:
            smBase = mmDCORE3_SYNC_MNGR_OBJS_BASE;
            break;
        default:
            VERIFY(false, "Invalid smIdx provided: {}", smIdx);
    }

    return smBase + varoffsetof(gaudi2::block_sob_objs, sob_obj[idx]);
}

unsigned hcl::Gaudi2HclScalUtils::getSOBIndex(uint32_t addr)
{
    return getSOBInfo(addr).sobId;
}

sob_info hcl::Gaudi2HclScalUtils::getSOBInfo(uint32_t addr)
{
    static constexpr uint32_t MAX_SIZE    = DCORE0_SYNC_MNGR_OBJS_MAX_OFFSET;
    static constexpr uint32_t DCORE0_BASE = mmDCORE0_SYNC_MNGR_OBJS_BASE & 0xffffffff;
    static constexpr uint32_t DCORE1_BASE = mmDCORE1_SYNC_MNGR_OBJS_BASE & 0xffffffff;
    static constexpr uint32_t DCORE2_BASE = mmDCORE2_SYNC_MNGR_OBJS_BASE & 0xffffffff;
    static constexpr uint32_t DCORE3_BASE = mmDCORE3_SYNC_MNGR_OBJS_BASE & 0xffffffff;

    sob_info ret = {0};

    if (addr >= DCORE0_BASE && addr < DCORE0_BASE + MAX_SIZE)
    {
        ret.dcore = 0;
        addr -= DCORE0_BASE;
    }
    else if (addr >= DCORE1_BASE && addr < DCORE1_BASE + MAX_SIZE)
    {
        ret.dcore = 1;
        addr -= DCORE1_BASE;
    }
    else if (addr >= DCORE2_BASE && addr < DCORE2_BASE + MAX_SIZE)
    {
        ret.dcore = 2;
        addr -= DCORE2_BASE;
    }
    else if (addr >= DCORE3_BASE && addr < DCORE3_BASE + MAX_SIZE)
    {
        ret.dcore = 3;
        addr -= DCORE3_BASE;
    }
    else
    {
        if (!GCFG_HCL_NULL_SUBMIT.value())
        {
            VERIFY(false, "Invalid address given: 0x{:x}", addr);
        }
    }

    addr -= varoffsetof(gaudi2::block_sob_objs, sob_obj[0]);
    VERIFY((addr & (sizeof(gaudi2::sob_objs::reg_sob_obj) - 1)) == 0, "Invalid address not divisible: 0x{:x}", addr);
    ret.sobId = addr >> 2;  // addr / sizeof(gaudi3::sob_objs::reg_sob_obj_0);
    ret.smIdx = ret.dcore;
    return ret;
}

std::string hcl::Gaudi2HclScalUtils::printSOBInfo(uint32_t addr)
{
    return printSOBInfo(getSOBInfo(addr));
}

std::string hcl::Gaudi2HclScalUtils::printSOBInfo(sob_info sob)
{
    return "DCORE" + std::to_string(sob.dcore) + "_SYNC_MNGR_OBJS SOB_OBJ_" + std::to_string(sob.sobId);
}

// return the gaudi2 value from QMAN FW gaudi2_arc_host_packets.h
uint32_t hcl::Gaudi2HclScalUtils::getCMaxTargetValue()
{
    return COMP_SYNC_GROUP_CMAX_TARGET;
}
