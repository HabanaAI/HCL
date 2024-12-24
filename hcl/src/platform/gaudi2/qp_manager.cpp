#include "platform/gaudi2/qp_manager.h"
#include "platform/gaudi2/hcl_device.h"
#include "hcl_dynamic_communicator.h"
#include "hcl_utils.h"

class HclDynamicCommunicator;

QPManagerGaudi2::QPManagerGaudi2(HclDeviceGaudi2& device) : QPManager(device)
{
    m_maxQPsPerConnection = m_device.getHal().getMaxQPsPerNic();
    VERIFY(m_maxQPsPerConnection == MAX_QPS_PER_CONNECTION_G2);
}

uint32_t QPManagerGaudi2::getQPi(const HCL_CollectiveOp collectiveOp, const bool isSend)
{
    switch (collectiveOp)
    {
        case eHCLReduceScatter:
            return isSend ? G2::QP_e::QPE_RS_SEND : G2::QP_e::QPE_RS_RECV;
            break;
        case eHCLAllGather:
            return isSend ? G2::QP_e::QPE_AG_SEND : G2::QP_e::QPE_AG_RECV;
            break;
        default:
            VERIFY(false, "invalid op({})", collectiveOp);
    }

    VERIFY(false, "unreachable code");
    return 0;
}

uint32_t QPManagerGaudi2::getDestQPi(const unsigned qpi) const
{
    switch (qpi)
    {
        case G2::QP_e::QPE_RS_RECV:
            return G2::QP_e::QPE_RS_SEND;
            break;
        case G2::QP_e::QPE_AG_RECV:
            return G2::QP_e::QPE_AG_SEND;
            break;
        case G2::QP_e::QPE_RS_SEND:
            return G2::QP_e::QPE_RS_RECV;
            break;
        case G2::QP_e::QPE_AG_SEND:
            return G2::QP_e::QPE_AG_RECV;
            break;
    }

    VERIFY(false, "unreachable code, qpi({})", qpi);

    return 0;
}