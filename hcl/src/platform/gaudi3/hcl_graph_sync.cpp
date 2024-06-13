#include "platform/gaudi3/hcl_graph_sync.h"

#include <cassert>  // for assert
#include <cstddef>  // for offsetof
#include "asic_reg/gaudi3_blocks.h"
#include "asic_reg_structs/sob_objs_regs.h"
#include "sched_pkts.h"  // for g2fw
#include "hcl_utils.h"
#include "infra/scal/gen2_arch_common/scal_utils.h"  // for varoffsetof
#include "infra/scal/gaudi3/scal_utils.h"
#include "gaudi3/asic_reg_structs/arc_acp_eng_regs.h"

class HclCommandsGen2Arch;

#define DCORE_SIZE (mmHD1_SYNC_MNGR_OBJS_BASE - mmHD0_SYNC_MNGR_OBJS_BASE)

// check if sm is at the beggining or the middle of a dcore
#define IS_EVEN_INDEXED_SM(smBase)                                                                                     \
    (((smBase - mmHD0_SYNC_MNGR_OBJS_BASE) & (DCORE_SIZE - 1)) == 0)  // hack to avoid modulas

HclGraphSyncGaudi3::HclGraphSyncGaudi3(unsigned smIdx, HclCommandsGen2Arch& commands)
: HclGraphSyncGen2Arch(smIdx, commands)
{
}

uint64_t HclGraphSyncGaudi3::getSyncManagerBase(unsigned smIdx)
{
    uint64_t smBase;
    // We have 2 SMs per dcore so if we devide it by 2 we get the dcore.
    switch (smIdx / 2)
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

    // for odd indexed SMs we need to jump to its offset from the begining of the dcore
    if (smIdx & 0x1)
    {
        smBase += offsetof(gaudi3::block_sob_objs, sob_obj_1);
    }

    return smBase;
}

uint32_t HclGraphSyncGaudi3::getAddrMonPayAddrl(uint64_t smBase, unsigned Idx)
{
    return varoffsetof(gaudi3::block_sob_objs, mon_pay_addrl_0[Idx]) + smBase;
}

uint32_t HclGraphSyncGaudi3::getAddrMonPayAddrh(uint64_t smBase, unsigned Idx)
{
    return varoffsetof(gaudi3::block_sob_objs, mon_pay_addrh_0[Idx]) + smBase;
}

uint32_t HclGraphSyncGaudi3::getAddrMonPayData(uint64_t smBase, unsigned Idx)
{
    return varoffsetof(gaudi3::block_sob_objs, mon_pay_data_0[Idx]) + smBase;
}

uint32_t HclGraphSyncGaudi3::getAddrMonConfig(uint64_t smBase, unsigned Idx)
{
    return varoffsetof(gaudi3::block_sob_objs, mon_config_0[Idx]) + smBase;
}

uint32_t HclGraphSyncGaudi3::getAddrSobObj(uint64_t smBase, unsigned Idx)
{
    return smBase + varoffsetof(gaudi3::block_sob_objs, sob_obj_0[Idx]);
}

uint32_t HclGraphSyncGaudi3::getRegSobObj(uint64_t smBase, unsigned Idx)
{
    // doesnt mater if it is index 0 or 1 since both structs are identical (reg_sob_obj_0/reg_sob_obj_1)
    return smBase + sizeof(gaudi3::sob_objs::reg_sob_obj_0) * Idx;
}

uint32_t HclGraphSyncGaudi3::getOffsetMonArm(unsigned Idx)
{
    // doesnt mater if it is index 0 or 1 since the offset from the SMBase is the same
    return varoffsetof(gaudi3::block_sob_objs, mon_arm_0[Idx]);
}

uint32_t HclGraphSyncGaudi3::createMonConfig(bool isLong, unsigned soQuarter)
{
    gaudi3::sob_objs::reg_mon_config_0 monConfig;
    monConfig._raw     = 0;
    monConfig.long_sob = isLong ? 1 : 0;
    monConfig.msb_sid  = soQuarter;  // TODO: Yaniv check
    return monConfig._raw;
}

uint32_t HclGraphSyncGaudi3::createSchedMonExpFence(unsigned /*fenceIdx*/)
{
    gaudi3::arc_acp_eng::reg_qsel_mask_counter maskCounter;

    const int op    = 1; // ADD
    const int value = 1;

    maskCounter._raw  = 0;
    maskCounter.op    = op;
    maskCounter.value = value;

    return maskCounter._raw;
}

uint32_t HclGraphSyncGaudi3::getArmMonSize()
{
    // doesnt mater if it is index 0 or 1 since both structs are identical (reg_mon_arm_0/reg_mon_arm_1)
    return sizeof(gaudi3::sob_objs::reg_mon_arm_0);
}

uint32_t HclGraphSyncGaudi3::createMonArm(uint64_t       soValue,
                                          bool           longMon,
                                          const uint8_t  mask,
                                          const unsigned soIdxNoMask,
                                          int            i,
                                          bool           useEqual)
{
    // doesnt mater if it is index 0 or 1 since both structs are identical (reg_mon_arm_0/reg_mon_arm_1)
    gaudi3::sob_objs::reg_mon_arm_0 monArm;

    monArm.sod  = getFifteenBits(soValue, i);
    monArm.sop  = useEqual ? 1 : 0;
    monArm.mask = longMon ? 0 : mask;
    monArm.sid  = (i == 0 ? (soIdxNoMask & 0xff) : 0);
    return monArm._raw;
}

uint32_t HclGraphSyncGaudi3::getSoConfigValue(unsigned value, bool isReduction)
{
    // doesnt mater if it is index 0 or 1 since both structs are identical (reg_sob_obj_0/reg_sob_obj_1)
    gaudi3::sob_objs::reg_sob_obj_0 soConfigMsg;
    soConfigMsg._raw = 0;

    VERIFY(value <= SO_MAX_VAL);
    soConfigMsg.val = value;
    soConfigMsg.inc = isReduction ? 1 : 0;

    return soConfigMsg._raw;
}
