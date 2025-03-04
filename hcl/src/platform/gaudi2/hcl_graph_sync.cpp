#include "platform/gaudi2/hcl_graph_sync.h"

#include <cstddef>                                            // for offsetof
#include "gaudi2/asic_reg/gaudi2_blocks.h"                    // for mmDCORE...
#include "gaudi2/asic_reg_structs/sob_objs_regs.h"            // for block_s...
#include "hcl_utils.h"                                        // for VERIFY
#include "infra/scal/gen2_arch_common/scal_stream.h"          // for ScalStream
#include "infra/scal/gen2_arch_common/scal_utils.h"           // for varoffs...
#include "infra/scal/gaudi2/scal_utils.h"                     // for Gaudi2HclScalU...
#include "platform/gen2_arch_common/commands/hcl_commands.h"  // for HclComm...
#include "g2_sched_pkts.h"                                    // for g2fw

HclGraphSyncGaudi2::HclGraphSyncGaudi2(unsigned syncSmIdx, HclCommandsGen2Arch& commands)
: HclGraphSyncGen2Arch(syncSmIdx, commands)
{
    m_utils = new hcl::Gaudi2HclScalUtils();
}

HclGraphSyncGaudi2::~HclGraphSyncGaudi2()
{
    if (m_utils) delete m_utils;
    m_utils = nullptr;
}

uint64_t HclGraphSyncGaudi2::getSyncManagerBase(unsigned smIdx)
{
    switch (smIdx)
    {
        case 0:
            return mmDCORE0_SYNC_MNGR_OBJS_BASE;
        case 1:
            return mmDCORE1_SYNC_MNGR_OBJS_BASE;
        case 2:
            return mmDCORE2_SYNC_MNGR_OBJS_BASE;
        case 3:
            return mmDCORE3_SYNC_MNGR_OBJS_BASE;
        default:
            VERIFY(false);
            return 0;
    }
}

uint32_t HclGraphSyncGaudi2::getAddrMonPayAddrl(uint64_t smBase, unsigned idx)
{
    return smBase + varoffsetof(gaudi2::block_sob_objs, mon_pay_addrl[idx]);
}

uint32_t HclGraphSyncGaudi2::getAddrMonPayAddrh(uint64_t smBase, unsigned idx)
{
    return smBase + varoffsetof(gaudi2::block_sob_objs, mon_pay_addrh[idx]);
}

uint32_t HclGraphSyncGaudi2::getAddrMonPayData(uint64_t smBase, unsigned idx)
{
    return smBase + varoffsetof(gaudi2::block_sob_objs, mon_pay_data[idx]);
}

uint32_t HclGraphSyncGaudi2::getAddrMonConfig(uint64_t smBase, unsigned idx)
{
    return smBase + varoffsetof(gaudi2::block_sob_objs, mon_config[idx]);
}

uint32_t HclGraphSyncGaudi2::getAddrSobObj(uint64_t smBase, unsigned idx)
{
    return smBase + varoffsetof(gaudi2::block_sob_objs, sob_obj[idx]);
}

uint64_t HclGraphSyncGaudi2::getFullRegSobObj(uint64_t smBase, unsigned idx)
{
    return smBase + sizeof(gaudi2::sob_objs::reg_sob_obj) * idx;
}

uint32_t HclGraphSyncGaudi2::getRegSobObj(uint64_t smBase, unsigned idx)
{
    return smBase + sizeof(gaudi2::sob_objs::reg_sob_obj) * idx;
}

uint32_t HclGraphSyncGaudi2::getOffsetMonArm(unsigned idx)
{
    return varoffsetof(gaudi2::block_sob_objs, mon_arm[idx]);
}

uint32_t HclGraphSyncGaudi2::createMonConfig(bool isLong, unsigned soQuarter)
{
    gaudi2::sob_objs::reg_mon_config monConfig;
    monConfig._raw     = 0;
    monConfig.long_sob = isLong ? 1 : 0;
    monConfig.msb_sid  = soQuarter;
    if (isLong)
    {
        monConfig.wr_num = 1;  // perform 2 writes - 2nd write is a dummy one as w/a for SM bug in H6 (SW-67146)
    }
    return monConfig._raw;
}

uint32_t HclGraphSyncGaudi2::getArmMonSize()
{
    return sizeof(gaudi2::sob_objs::reg_mon_arm);
}

uint32_t HclGraphSyncGaudi2::createMonArm(uint64_t       soValue,
                                          bool           longMon,
                                          const uint8_t  mask,
                                          const unsigned soIdxNoMask,
                                          int            i,
                                          bool           useEqual)
{
    gaudi2::sob_objs::reg_mon_arm monArm;

    monArm.sod  = getFifteenBits(soValue, i);
    monArm.sop  = useEqual ? 1 : 0;
    monArm.mask = longMon ? 0 : mask;
    monArm.sid  = (i == 0 ? (soIdxNoMask & 0xff) : 0);

    return monArm._raw;
}

uint32_t HclGraphSyncGaudi2::createSchedMonExpFence(unsigned fenceIdx)
{
    g2fw::sched_mon_exp_fence_t updateMessage;
    updateMessage.opcode   = g2fw::MON_EXP_FENCE_UPDATE;
    updateMessage.fence_id = fenceIdx;
    updateMessage.reserved = 0;
    return updateMessage.raw;
}

uint32_t HclGraphSyncGaudi2::getSoConfigValue(unsigned value, bool isReduction)
{
    gaudi2::sob_objs::reg_sob_obj soConfigMsg;
    soConfigMsg._raw = 0;

    VERIFY(value <= SO_MAX_VAL);
    soConfigMsg.val = value;
    soConfigMsg.inc = isReduction ? 1 : 0;

    return soConfigMsg._raw;
}

void HclGraphSyncGaudi2::createSetupMonMessages(hcl::ScalStream& scalStream,
                                                uint64_t         address,
                                                unsigned         fenceIdx,
                                                unsigned         monitorIdx,
                                                uint64_t         smBase,
                                                bool             isLong)
{
    HclGraphSyncGen2Arch::createSetupMonMessages(scalStream, address, fenceIdx, monitorIdx, smBase, isLong);

    unsigned schedIdx = scalStream.getSchedIdx();
    if (isLong)
    {
        LBWBurstData_t destData;
        // 2nd dummy message to DCORE0_SYNC_MNGR_OBJS SOB_OBJ_8184
        uint64_t address2 = offsetof(gaudi2::block_sob_objs, sob_obj[8184]) + smBase;
        // Setup to payload address (of dummy SOB)

        uint32_t destination = varoffsetof(gaudi2::block_sob_objs, mon_pay_addrl[monitorIdx + 1]) + smBase;
        destData.push_back({destination, (uint32_t)address2 & 0xffffffff});

        destination = varoffsetof(gaudi2::block_sob_objs, mon_pay_addrh[monitorIdx + 1]) + smBase;
        destData.push_back({destination, (uint32_t)((address2 >> 32) & 0xffffffff)});

        // Create a dummy data that would be written to SOB_OBJ_8184
        destination = varoffsetof(gaudi2::block_sob_objs, mon_pay_data[monitorIdx + 1]) + smBase;
        destData.push_back({destination, 0});

        m_commands.serializeLbwBurstWriteCommand(scalStream, schedIdx, destData);
    }
}
