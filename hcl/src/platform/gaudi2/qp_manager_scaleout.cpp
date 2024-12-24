#include "qp_manager_scaleout.h"
#include "platform/gaudi2/qp_manager.h"
#include "platform/gaudi2/hcl_device.h"
#include "hcl_dynamic_communicator.h"
#include "hcl_utils.h"

class HclDynamicCommunicator;

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

void QPManagerGaudi2ScaleOut::addQPsToQPManagerDB(const QPManagerHints& hints, const QpsVector& qps)
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

void QPManagerGaudi2ScaleOut::ReleaseQPsResource(const QPManagerHints& hints)
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
