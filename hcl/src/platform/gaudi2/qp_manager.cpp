#include "platform/gaudi2/qp_manager.h"
#include "platform/gaudi2/hcl_device.h"
#include "hcl_dynamic_communicator.h"
#include "hcl_utils.h"

class HclDynamicCommunicator;

QPManagerGaudi2::QPManagerGaudi2(HclDeviceGaudi2& device) : QPManager(device)
{
    m_maxQPsPerConnection = m_device.getHal()->getMaxQPsPerNic();
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

/* ScaleUp QP Manager */

QPManagerGaudi2ScaleUp::QPManagerGaudi2ScaleUp(HclDeviceGaudi2& device) : QPManagerGaudi2(device)
{
    m_qpInfoScaleUp.resize(DEFAULT_COMMUNICATORS_SIZE);

    for (auto& nic : m_qpInfoScaleUp)
    {
        for (auto& qpi : nic)
        {
            qpi.fill(INVALID_QP);
        }
    }
}

void QPManagerGaudi2ScaleUp::resizeDBForNewComms(const HCL_Comm comm)
{
    const size_t oldSize = m_qpInfoScaleUp.size();
    const size_t newSize = oldSize + DEFAULT_COMMUNICATORS_SIZE;

    LOG_HCL_TRACE(HCL, "resizing m_qpInfoScaleUp for new comm {} from {} to {}", comm, oldSize, newSize);

    m_qpInfoScaleUp.resize(newSize);
    for (unsigned index = oldSize; index < newSize; index++)
    {
        for (auto& qpi : m_qpInfoScaleUp.at(index))
        {
            qpi.fill(INVALID_QP);
        }
    }
}

void QPManagerGaudi2ScaleUp::registerQPs(const QPManagerHints& hints, const QpsVector& qps)
{
    const HCL_Comm comm = hints.m_comm;
    const unsigned nic  = hints.m_nic;

    if (comm >= m_qpInfoScaleUp.size())
    {
        resizeDBForNewComms(comm);
    }

    for (unsigned qpi = 0; qpi < m_maxQPsPerConnection; qpi++)
    {
        m_qpInfoScaleUp.at(comm).at(nic).at(qpi) = qps.at(qpi);

        LOG_HCL_DEBUG(HCL,
                      "m_qpInfoScaleUp[comm {}][nic {}][qpi {}] = qpn {}",
                      comm,
                      nic,
                      qpi,
                      m_qpInfoScaleUp.at(comm).at(nic).at(qpi));
    }
}

void QPManagerGaudi2ScaleUp::closeQPs(const QPManagerHints& hints)
{
    const HCL_Comm comm = hints.m_comm;
    const unsigned nic  = hints.m_nic;

    const UniqueSortedVector& ranks = m_device.getComm(comm).getInnerRanksExclusive();
    if (ranks.size() == 0) return;

    for (unsigned qpi = 0; qpi < m_maxQPsPerConnection; qpi++)
    {
        const uint32_t qpn = m_qpInfoScaleUp.at(comm).at(nic).at(qpi);
        if (isInvalidQPn(qpn)) continue;

        LOG_HCL_TRACE(HCL, "closing QP: comm({}) nic({}) qpi({}) qpn({})", comm, nic, qpi, qpn);

        m_device.destroyQp(nic, qpn);
        m_qpInfoScaleUp.at(comm).at(nic).at(qpi) = 0;
    }
}

uint32_t QPManagerGaudi2ScaleUp::getQPn(const QPManagerHints& hints) const
{
    const HCL_Comm comm = hints.m_comm;
    const unsigned nic  = hints.m_nic;
    const unsigned qpi  = hints.m_qpi;

    return m_qpInfoScaleUp.at(comm).at(nic).at(qpi);
}

uint32_t QPManagerGaudi2ScaleUp::getQPi(const QPManagerHints& hints) const
{
    const HCL_Comm comm = hints.m_comm;
    const unsigned nic  = hints.m_nic;
    const unsigned qpn  = hints.m_qpn;

    for (unsigned qpi = 0; qpi < m_maxQPsPerConnection; qpi++)
    {
        if (m_qpInfoScaleUp.at(comm).at(nic).at(qpi) == qpn)
        {
            return qpi;
        }
    }

    VERIFY(false, "could not find a match for comm {} nic {} qpn {}", comm, nic, qpn);
    return 0;
}

/* ScaleOut QP Manager */

QPManagerGaudi2ScaleOut::QPManagerGaudi2ScaleOut(HclDeviceGaudi2& device) : QPManagerGaudi2(device) {}

void QPManagerGaudi2ScaleOut::resizeDBForNewComms(const HCL_Comm comm)
{
    const size_t oldSize = m_qpInfoScaleOut.size();
    const size_t newSize = oldSize + DEFAULT_COMMUNICATORS_SIZE;

    LOG_HCL_TRACE(HCL, "resizing m_qpInfoScaleOut for new comm {} from {} to {}", comm, oldSize, newSize);

    m_qpInfoScaleOut.resize(newSize);
    for (unsigned index = oldSize; index < newSize; index++)
    {
        for (auto& nic : m_qpInfoScaleOut.at(index))
        {
            for (auto& qpSet : nic)
            {
                for (auto& qpi : qpSet)
                {
                    qpi.fill(INVALID_QP);
                }
            }
        }
    }
}

void QPManagerGaudi2ScaleOut::resizeDBPerComm(const HCL_Comm comm)
{
    const size_t commSize = m_device.getCommSize(comm);

    LOG_HCL_TRACE(HCL, "resizing m_qpInfoScaleOut[comm {}] to commSize {}", comm, commSize);

    m_qpInfoScaleOut.at(comm).resize(commSize);

    for (auto& nic : m_qpInfoScaleOut.at(comm))
    {
        for (auto& qpSet : nic)
        {
            for (auto& qpi : qpSet)
            {
                qpi.fill(INVALID_QP);
            }
        }
    }
}

void QPManagerGaudi2ScaleOut::allocateQPDBStorage(const HCL_Comm comm)
{
    if (comm >= m_qpInfoScaleOut.size())
    {
        resizeDBForNewComms(comm);
    }

    if (m_qpInfoScaleOut[comm].size() == 0)
    {
        resizeDBPerComm(comm);
    }
}

void QPManagerGaudi2ScaleOut::registerQPs(const QPManagerHints& hints, const QpsVector& qps)
{
    const HCL_Comm comm       = hints.m_comm;
    const unsigned nic        = hints.m_nic;
    const unsigned remoteRank = hints.m_remoteRank;

    if (comm >= m_qpInfoScaleOut.size())
    {
        resizeDBForNewComms(comm);
    }
    if (m_qpInfoScaleOut.at(comm).size() == 0)
    {
        resizeDBPerComm(comm);
    }

    const unsigned subNicIndex = m_device.getServerConnectivity().getSubPortIndex(nic, comm);
    for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
    {
        for (unsigned qpi = 0; qpi < m_maxQPsPerConnection; qpi++)
        {
            const unsigned qpIndex = (m_maxQPsPerConnection * qpSet) + qpi;
            if (qpIndex >= qps.size()) break;

            uint32_t qpn = qps.at(qpIndex);

            m_qpInfoScaleOut.at(comm).at(remoteRank).at(subNicIndex).at(qpSet).at(qpi) = qpn;
            LOG_HCL_DEBUG(HCL,
                          "m_qpInfoScaleOut[comm {}][rank {}][subNic {}][set {}][qpi {}] = qpn {}",
                          comm,
                          remoteRank,
                          subNicIndex,
                          qpSet,
                          qpi,
                          m_qpInfoScaleOut.at(comm).at(remoteRank).at(subNicIndex).at(qpSet).at(qpi));
        }
    }
}

void QPManagerGaudi2ScaleOut::closeQPs(const QPManagerHints& hints)
{
    const HCL_Comm            comm        = hints.m_comm;
    const unsigned            nic         = hints.m_nic;
    const unsigned            subNicIndex = m_device.getServerConnectivity().getSubPortIndex(nic, comm);
    const UniqueSortedVector& ranks       = m_device.getComm(comm).getOuterRanksExclusive();

    // in HNIC flows we do not open or register scaleout QPs, so do not need to close any
    if (m_qpInfoScaleOut.size() == 0) return;

    for (auto& rank : ranks)
    {
        for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
        {
            for (unsigned qpi = 0; qpi < m_maxQPsPerConnection; qpi++)
            {
                const uint32_t qpn = m_qpInfoScaleOut.at(comm).at(rank).at(subNicIndex).at(qpSet).at(qpi);
                if (isInvalidQPn(qpn)) continue;

                LOG_HCL_TRACE(HCL,
                              "closing QP: comm({}) rank({}) nic({}) qpSet{} qpi({}) qpn({})",
                              comm,
                              rank,
                              nic,
                              qpSet,
                              qpi,
                              qpn);

                m_device.destroyQp(nic, qpn);
                m_qpInfoScaleOut.at(comm).at(rank).at(subNicIndex).at(qpSet).at(qpi) = 0;
            }
        }
    }
}

uint32_t QPManagerGaudi2ScaleOut::getQPn(const QPManagerHints& hints) const
{
    const HCL_Comm comm       = hints.m_comm;
    const unsigned remoteRank = hints.m_remoteRank;
    const unsigned nic        = hints.m_nic;
    const unsigned qpSet      = hints.m_qpSet;
    const unsigned qpi        = hints.m_qpi;

    const uint8_t subNicIndex = m_device.getServerConnectivity().getSubPortIndex(nic, comm);
    return m_qpInfoScaleOut.at(comm).at(remoteRank).at(subNicIndex).at(qpSet).at(qpi);
}

uint32_t QPManagerGaudi2ScaleOut::getQPi(const QPManagerHints& hints) const
{
    const HCL_Comm comm       = hints.m_comm;
    const unsigned nic        = hints.m_nic;
    const unsigned remoteRank = hints.m_remoteRank;
    const unsigned qpn        = hints.m_qpn;

    const uint8_t subNicIndex = m_device.getServerConnectivity().getSubPortIndex(nic, comm);

    for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
    {
        for (unsigned qpi = 0; qpi < m_maxQPsPerConnection; qpi++)
        {
            if (m_qpInfoScaleOut.at(comm).at(remoteRank).at(subNicIndex).at(qpSet).at(qpi) == qpn)
            {
                return qpi;
            }
        }
    }

    VERIFY(false,
           "could not find a match for comm {} rank {} nix {} (subNic {}) qpn {}",
           comm,
           remoteRank,
           nic,
           subNicIndex,
           qpn);
    return 0;
}
