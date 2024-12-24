#include "qp_manager.h"

#include <ext/alloc_traits.h>  // for __alloc_traits<>::value...
#include <algorithm>           // for max
#include <cstdint>             // for uint32_t, uint8_t

#include "hcl_utils.h"                   // for VERIFY
#include "platform/gaudi3/hal.h"         // for Gaudi3Hal
#include "platform/gaudi3/hcl_device.h"  // for HclDeviceGaudi3
#include "platform/gaudi3/commands/hcl_commands.h"
#include "hcl_math_utils.h"
#include "platform/gen2_arch_common/server_connectivity.h"    // for Gen2ArchServerConnectivity
#include "platform/gaudi3/gaudi3_base_server_connectivity.h"  // for Gaudi3BaseServerConnectivity

QPManagerGaudi3::QPManagerGaudi3(HclDeviceGaudi3& device) : QPManager(device)
{
    m_maxQPsPerConnection = device.getGaudi3Hal().getMaxQPsPerNic();
    VERIFY(m_maxQPsPerConnection == MAX_QPS_PER_CONNECTION_G3);
}

uint32_t QPManagerGaudi3::getQPi(const HCL_CollectiveOp collectiveOp, const bool isSend)
{
    switch (collectiveOp)
    {
        case eHCLReduceScatter:
            return isSend ? G3::QP_e::QPE_RS_SEND : G3::QP_e::QPE_RS_RECV;
            break;
        case eHCLAllGather:
            return isSend ? G3::QP_e::QPE_AG_SEND : G3::QP_e::QPE_AG_RECV;
            break;
        case eHCLAll2All:
            return isSend ? G3::QP_e::QPE_A2A_SEND : G3::QP_e::QPE_A2A_RECV;
            break;
        default:
            VERIFY(false, "invalid op({})", collectiveOp);
    }

    VERIFY(false, "unreachable code");
    return 0;
}

uint32_t QPManagerGaudi3::getDestQPi(const unsigned qpi) const
{
    switch (qpi)
    {
        case G3::QP_e::QPE_RS_RECV:
            return G3::QP_e::QPE_RS_SEND;
            break;
        case G3::QP_e::QPE_AG_RECV:
            return G3::QP_e::QPE_AG_SEND;
            break;
        case G3::QP_e::QPE_RS_SEND:
            return G3::QP_e::QPE_RS_RECV;
            break;
        case G3::QP_e::QPE_AG_SEND:
            return G3::QP_e::QPE_AG_RECV;
            break;
        case G3::QP_e::QPE_A2A_SEND:
            return G3::QP_e::QPE_A2A_RECV;
            break;
        case G3::QP_e::QPE_A2A_RECV:
            return G3::QP_e::QPE_A2A_SEND;
            break;
    }

    VERIFY(false, "unreachable code");

    return 0;
}

QPUsage QPManagerGaudi3::getBaseQpAndUsage(const HclDynamicCommunicator& dynamicComm,
                                           HCL_CollectiveOp              collectiveOp,
                                           bool                          isSend,
                                           bool                          isComplexCollective,
                                           bool                          isReductionInIMB,
                                           bool                          isHierarchical,
                                           uint64_t                      count,
                                           uint64_t                      cellCount,
                                           HclConfigType                 boxType,
                                           bool                          isScaleOut,
                                           HCL_Rank                      remoteRank,
                                           uint8_t                       qpSet,
                                           const bool                    isReduction,
                                           HCL_CollectiveOp              complexCollective,
                                           bool                          isRoot)
{
    QPUsage ret = {0, false};

    G3::QP_e qpi;
    bool     outOfBounds = count != INVALID_COUNT &&
                       ((cellCount * mod(dynamicComm.getMyRank(), dynamicComm.getScaleupGroupSize())) >= count);
    switch (collectiveOp)
    {
        case eHCLReduceScatter:
            if (isSend)
            {
                qpi = G3::QP_e::QPE_RS_SEND;
            }
            else if (isComplexCollective && !isReductionInIMB && (!isHierarchical || outOfBounds))
            {
                if (complexCollective == eHCLReduce && !isRoot && !outOfBounds)
                {
                    ret.disregardRank = true;
                }
                qpi = G3::QP_e::QPE_RS_RECV;
            }
            else if ((isComplexCollective && isReductionInIMB && outOfBounds) || isReduction)
            {
                qpi = G3::QP_e::QPE_RS_RECV;
            }
            else if (complexCollective == eHCLReduce && isRoot && !isReductionInIMB && isHierarchical)
            {
                qpi = G3::QP_e::QPE_RS_RECV;
            }
            else
            {
                qpi               = G3::QP_e::QPE_RS_RECV;
                ret.disregardRank = true;
            }
            break;
        case eHCLGather:  // FALLTHROUGH
        case eHCLAllGather:
            if (isSend)
            {
                qpi = G3::QP_e::QPE_AG_SEND;
                if (!isComplexCollective || collectiveOp == eHCLGather)
                {
                    ret.disregardRank = true;
                }
            }
            else
            {
                qpi = G3::QP_e::QPE_AG_RECV;
            }
            break;
        case eHCLAll2All:
            if (isScaleOut)
            {
                if (isSend)
                {
                    qpi = G3::QP_e::QPE_RS_SEND;
                }
                else
                {
                    qpi = G3::QP_e::QPE_RS_RECV;
                }
            }
            else
            {
                if (isSend)
                {
                    qpi = G3::QP_e::QPE_A2A_SEND;
                }
                else
                {
                    qpi = G3::QP_e::QPE_A2A_RECV;
                }
            }
            break;
        case eHCLReduce:
        case eHCLScatter:
            if (boxType == LOOPBACK) ret.disregardRank = true;
            if (isSend)
            {
                qpi = G3::QP_e::QPE_RS_SEND;
            }
            else
            {
                qpi               = G3::QP_e::QPE_RS_RECV;
                ret.disregardRank = true;
            }
            break;
        case eHCLBroadcast:            // FALLTHROUGH
        case eHCLSinglePeerBroadcast:  // FALLTHROUGH
        case eHCLSimpleBroadcast:
            if (isSend)
            {
                qpi = G3::QP_e::QPE_AG_SEND;
            }
            else
            {
                qpi = G3::QP_e::QPE_AG_RECV;
            }
            ret.disregardRank = true;
            break;
        case eHCLNoCollective:  // send recv
            if (isSend)
            {
                qpi = G3::QP_e::QPE_RS_SEND;
            }
            else
            {
                qpi = G3::QP_e::QPE_RS_RECV;
            }
            ret.disregardRank = true;
            break;
        default:
            VERIFY(false, "Cannot run collectiveOp {} on Gaudi3 device", (int)collectiveOp);
    }

    const QPManagerHints hints(dynamicComm, remoteRank, INVALID_QP, qpi, INVALID_QP, qpSet);
    ret.qpn = getQPn(hints);

    // we use offset 0 for all collective in scaleOut
    if (isScaleOut) ret.disregardRank = true;

    return ret;
}
