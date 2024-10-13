#include "infra/scal/gaudi3/scal_utils.h"

#include <cassert>                           // for assert
#include <cstddef>                           // for offsetof
#include "asic_reg/gaudi3_blocks.h"          // for mmHD0_SYNC_MNGR_O...
#include "asic_reg_structs/sob_objs_regs.h"  // for block_sob_objs
#include "hcl_utils.h"                       // for VERIFY
#include "gaudi3/gaudi3_arc_host_packets.h"  // for gaudi3 FW COMP_SYNC_GROUP_CMAX_TARGET

uint64_t hcl::Gaudi3HclScalUtils::calculateSoAddressFromIdxAndSM(unsigned smIdx, unsigned idx)
{
    uint64_t smBase;
    // We have 2 SMs per dcore so if we divide it by 2 we get the dcore.
    switch (smIdx >> 1)
    {
        case 0:
            smBase = mmHD0_SYNC_MNGR_OBJS_BASE;
            break;
        case 1:
            smBase = mmHD1_SYNC_MNGR_OBJS_BASE;
            break;
        case 2:
            smBase = mmHD2_SYNC_MNGR_OBJS_BASE;
            break;
        case 3:
            smBase = mmHD3_SYNC_MNGR_OBJS_BASE;
            break;
        case 4:
            smBase = mmHD4_SYNC_MNGR_OBJS_BASE;
            break;
        case 5:
            smBase = mmHD5_SYNC_MNGR_OBJS_BASE;
            break;
        case 6:
            smBase = mmHD6_SYNC_MNGR_OBJS_BASE;
            break;
        case 7:
            smBase = mmHD7_SYNC_MNGR_OBJS_BASE;
            break;
        default:
            assert(0);
            return 0;
    }

    // for odd indexed SMs we need to jump to its offset from the beginning of the dcore
    if (smIdx & 0x1)
    {
        smBase += offsetof(gaudi3::block_sob_objs, sob_obj_1);
    }

    // add the offset for the specific SOB
    smBase += varoffsetof(gaudi3::block_sob_objs, sob_obj_0[idx]);

    return smBase;
}

unsigned hcl::Gaudi3HclScalUtils::getSOBIndex(uint32_t addr)
{
    return getSOBInfo(addr).sobId;
}

sob_info hcl::Gaudi3HclScalUtils::getSOBInfo(uint32_t addr)
{
    // max size of an sm block
    static constexpr uint32_t MAX_SM_SIZE = offsetof(gaudi3::block_sob_objs, cq_direct);
    static constexpr uint32_t HD0_BASE    = mmHD0_SYNC_MNGR_OBJS_BASE & 0xffffffff;
    static constexpr uint32_t BASE_MASK   = 0xfff00000;

    sob_info ret = {0};

    /*
    HD0_BASE=0xfe380000
    HD1_BASE=0xfe780000
    HD2_BASE=0xfeb80000
    HD3_BASE=0xfef80000
    HD4_BASE=0xff380000
    HD5_BASE=0xff780000
    HD6_BASE=0xffb80000
    HD7_BASE=0xfff80000
    we would like to find out what halfDcore the address belongs to. here are the steps:
    1. please note: HD<i>_BASE' = 0xfe300000 + 0x400000 * i + 0x80000
    2. addr & 0xfff00000 = 0xfe300000 + 0x400000 * i
    3. (addr & 0xfff00000) - 0xfe300000 = 0x400000 * i
    4. i = ((addr & 0xfff00000) - 0xfe300000)/0x400000
    */
    uint32_t base = (addr & BASE_MASK);
    ret.dcore     = (base - (HD0_BASE & BASE_MASK)) >> 22;

    // get the SM address relative to the HD
    addr -= (base + (HD0_BASE & ~BASE_MASK));

    ret.ssm = (addr >= MAX_SM_SIZE);
    // there are 2 SMs per dcore, so to get the smIdx need to multiply dcore and add ssm (e.g. dcore3 -> smIdx 6,7)
    ret.smIdx = (ret.dcore << 1) + ret.ssm;

    // get the sob address relative to the SM
    addr -= (varoffsetof(gaudi3::block_sob_objs, sob_obj_1) * ret.ssm) +
            (varoffsetof(gaudi3::block_sob_objs, sob_obj_0) * ((uint8_t)!ret.ssm));

    VERIFY((addr & (sizeof(gaudi3::sob_objs::reg_sob_obj_0) - 1)) == 0, "Invalid address not divisible: 0x{:x}", addr);

    // divide by 4 to get the index from the offset
    ret.sobId = addr >> 2;  // addr / sizeof(gaudi3::sob_objs::reg_sob_obj_0)
    return ret;
}

std::string hcl::Gaudi3HclScalUtils::printSOBInfo(uint32_t addr)
{
    return printSOBInfo(getSOBInfo(addr));
}

std::string hcl::Gaudi3HclScalUtils::printSOBInfo(sob_info sob)
{
    return "HD" + std::to_string(sob.dcore) + "_SYNC_MNGR_OBJS SOB_OBJ_" + std::to_string(sob.ssm) + "_" +
           std::to_string(sob.sobId);
}

// return the gaudi3 value from QMAN FW gaudi3_arc_host_packets.h
uint32_t hcl::Gaudi3HclScalUtils::getCMaxTargetValue()
{
    return COMP_SYNC_GROUP_CMAX_TARGET;
}
