#include "scal/gen2_arch_common/scal_exceptions.h"
#include "infra/scal/gaudi3/scal_wrapper.h"

#include "infra/scal/gaudi3/scal_utils.h"            // for Gaudi3HclScalUtils
#include "infra/scal/gen2_arch_common/scal_utils.h"  // for Gen2ArchScalUtils

#include "gaudi3/asic_reg_structs/arc_acp_eng_regs.h"  // block_arc_acp_eng
#include "sched_pkts.h"                                // for g3fw

namespace hcl
{
class ScalJsonNames;
}

using namespace hcl;

Gaudi3ScalWrapper::Gaudi3ScalWrapper(scal_handle_t deviceHandle, ScalJsonNames& scalNames)
: Gen2ArchScalWrapper(deviceHandle, scalNames)
{
    m_utils = new Gaudi3HclScalUtils();
}

Gaudi3ScalWrapper::Gaudi3ScalWrapper(int fd, ScalJsonNames& scalNames) : Gen2ArchScalWrapper(fd, scalNames)
{
    m_utils = new Gaudi3HclScalUtils();
}

Gaudi3ScalWrapper::~Gaudi3ScalWrapper()
{
    if (m_utils) delete m_utils;
}

uint64_t Gaudi3ScalWrapper::getArcAcpEng(unsigned smIndex) const
{
    uint64_t smBase = 0;

    switch (smIndex)
    {
        case 0:
            smBase = mmHD0_ARC_FARM_ARC0_ACP_ENG_BASE;
            break;
        case 1:
            smBase = mmHD0_ARC_FARM_ARC1_ACP_ENG_BASE;
            break;

        case 2:
            smBase = mmHD1_ARC_FARM_ARC0_ACP_ENG_BASE;
            break;
        case 3:
            smBase = mmHD1_ARC_FARM_ARC1_ACP_ENG_BASE;
            break;

        case 4:
            smBase = mmHD2_ARC_FARM_ARC0_ACP_ENG_BASE;
            break;
        case 5:
            smBase = mmHD2_ARC_FARM_ARC1_ACP_ENG_BASE;
            break;

        case 6:
            smBase = mmHD3_ARC_FARM_ARC0_ACP_ENG_BASE;
            break;
        case 7:
            smBase = mmHD3_ARC_FARM_ARC1_ACP_ENG_BASE;
            break;

        case 8:
            smBase = mmHD4_ARC_FARM_ARC0_ACP_ENG_BASE;
            break;
        case 9:
            smBase = mmHD4_ARC_FARM_ARC1_ACP_ENG_BASE;
            break;

        case 10:
            smBase = mmHD5_ARC_FARM_ARC0_ACP_ENG_BASE;
            break;
        case 11:
            smBase = mmHD5_ARC_FARM_ARC1_ACP_ENG_BASE;
            break;

        case 12:
            smBase = mmHD6_ARC_FARM_ARC0_ACP_ENG_BASE;
            break;
        case 13:
            smBase = mmHD6_ARC_FARM_ARC1_ACP_ENG_BASE;
            break;

        case 14:
            smBase = mmHD7_ARC_FARM_ARC0_ACP_ENG_BASE;
            break;
        case 15:
            smBase = mmHD7_ARC_FARM_ARC1_ACP_ENG_BASE;
            break;

        default:
            assert(0);
    }

    return smBase;
}

uint64_t Gaudi3ScalWrapper::getMonitorPayloadAddr(std::string name, unsigned fenceIdx)
{
    scal_core_handle_t schedulerHandle;

    int rc = scal_get_core_handle_by_name(m_deviceHandle, name.c_str(), &schedulerHandle);
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on scal_get_core_handle_by_name with device handle: " +
                                 std::to_string(uint64_t(m_deviceHandle)) + " and name: " + name);
    }

    scal_control_core_infoV2_t coreInfo;

    rc = scal_control_core_get_infoV2(schedulerHandle, &coreInfo);
    if (rc != 0)
    {
        throw ScalErrorException("Failed on scal_control_core_get_info with core handle: " +
                                 std::to_string(uint64_t(schedulerHandle)));
    }

    return getArcAcpEng(coreInfo.idx) + varoffsetof(gaudi3::block_arc_acp_eng, qsel_mask_counter[fenceIdx]);
}
