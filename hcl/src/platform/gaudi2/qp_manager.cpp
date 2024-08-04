#include "platform/gaudi2/qp_manager.h"
#include "platform/gaudi2/hcl_device.h"
#include "hcl_utils.h"

QPManagerGaudi2::QPManagerGaudi2(HclDeviceGaudi2* device) : m_device(device) {}

QPManagerScaleUpGaudi2::QPManagerScaleUpGaudi2(HclDeviceGaudi2* device) : QPManagerGaudi2(device)
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

void QPManagerScaleUpGaudi2::resizeDB(HCL_Comm comm)
{
    const size_t oldSize = m_qpInfoScaleUp.size();
    const size_t newSize = oldSize + DEFAULT_COMMUNICATORS_SIZE;

    m_qpInfoScaleUp.resize(newSize);
    for (unsigned index = oldSize; index < newSize; index++)
    {
        for (auto& qpi : m_qpInfoScaleUp.at(index))
        {
            qpi.fill(INVALID_QP);
        }
    }

    LOG_HCL_TRACE(HCL, "resizing m_qpInfoScaleUp for new comm {} from {} to {}", comm, oldSize, newSize);
}

void QPManagerScaleUpGaudi2::registerQPs(HCL_Comm         comm,
                                         uint8_t          nic,
                                         const QpsVector& qps,
                                         HCL_Rank         remoteRank,
                                         uint32_t         commSize,
                                         unsigned         qpSets)
{
    if (comm >= m_qpInfoScaleUp.size())
    {
        resizeDB(comm);
    }

    for (unsigned qpi = 0; qpi < MAX_QPS_PER_CONNECTION_G2; qpi++)
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

uint32_t QPManagerScaleUpGaudi2::getQP(HCL_Comm       comm,
                                       const uint8_t  nic,
                                       const unsigned qpi,
                                       const uint8_t  qpSet,
                                       const HCL_Rank remoteRank)
{
    return m_qpInfoScaleUp.at(comm).at(nic).at(qpi);
}

uint32_t QPManagerScaleUpGaudi2::getQPi(HCL_Comm comm, const uint8_t nic, const unsigned qpn, const HCL_Rank remoteRank)
{
    for (unsigned qpi = 0; qpi < MAX_QPS_PER_CONNECTION_G2; qpi++)
    {
        if (m_qpInfoScaleUp.at(comm).at(nic).at(qpi) == qpn)
        {
            return qpi;
        }
    }

    VERIFY(false, "could not find a match for comm {} nic {} qpn {}", comm, nic, qpn);
    return 0;
}

void QPManagerScaleUpGaudi2::closeQPs(HCL_Comm comm, const UniqueSortedVector& ranks)
{
    if (ranks.size() == 0) return;

    for (unsigned nic = 0; nic < MAX_NICS_GEN2ARCH; nic++)
    {
        for (unsigned qpi = 0; qpi < MAX_QPS_PER_CONNECTION_G2; qpi++)
        {
            uint32_t qpn = m_qpInfoScaleUp.at(comm).at(nic).at(qpi);
            if (isInvalidQPn(qpn)) continue;

            LOG_HCL_TRACE(HCL, "closing QP: comm({}) nic({}) qpi({}) qpn({})", comm, nic, qpi, qpn);

            m_device->destroyQp(nic, qpn);

            m_qpInfoScaleUp.at(comm).at(nic).at(qpi) = 0;
        }
    }
}

/* ScaleOut QP Manager*/

QPManagerScaleOutGaudi2::QPManagerScaleOutGaudi2(HclDeviceGaudi2* device, Gaudi2DevicePortMapping& portMapping)
: QPManagerGaudi2(device), m_portMapping(portMapping)
{
    m_qpInfoScaleOut.resize(DEFAULT_COMMUNICATORS_SIZE);
    for (auto& rank : m_qpInfoScaleOut)
    {
        for (auto& nic : rank)
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

void QPManagerScaleOutGaudi2::resizeDB(HCL_Comm comm)
{
    const size_t oldSize = m_qpInfoScaleOut.size();
    const size_t newSize = oldSize + DEFAULT_COMMUNICATORS_SIZE;

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

    LOG_HCL_TRACE(HCL, "resizing m_qpInfoScaleOut for new comm {} from {} to {}", comm, oldSize, newSize);
}

void QPManagerScaleOutGaudi2::resizeDBForComm(HCL_Comm comm, const uint32_t commSize)
{
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

void QPManagerScaleOutGaudi2::allocateCommQPs(HCL_Comm comm, const uint32_t commSize)
{
    if (m_qpInfoScaleOut[comm].size() == 0)
    {
        resizeDBForComm(comm, commSize);
    }
}

void QPManagerScaleOutGaudi2::registerQPs(HCL_Comm         comm,
                                          uint8_t          nic,
                                          const QpsVector& qps,
                                          HCL_Rank         remoteRank,
                                          uint32_t         commSize,
                                          const unsigned   qpSets)
{
    VERIFY(qpSets <= MAX_QPS_SETS_PER_CONNECTION);

    if (comm >= m_qpInfoScaleOut.size())
    {
        resizeDB(comm);
    }
    if (m_qpInfoScaleOut.at(comm).size() == 0)
    {
        resizeDBForComm(comm, commSize);
    }

    const unsigned subNicIndex = m_portMapping.getSubPortIndex(nic);
    for (unsigned qpSet = 0; qpSet < qpSets; qpSet++)
    {
        for (unsigned qpi = 0; qpi < MAX_QPS_PER_CONNECTION_G2; qpi++)
        {
            unsigned qpIndex = (MAX_QPS_PER_CONNECTION_G2 * qpSet) + qpi;
            uint32_t qpn     = qpIndex < qps.size() ? qps.at(qpIndex) : INVALID_QP;

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

uint32_t QPManagerScaleOutGaudi2::getQP(HCL_Comm       comm,
                                        const uint8_t  nic,
                                        const unsigned qpi,
                                        const uint8_t  qpSet,
                                        const HCL_Rank remoteRank)
{
    uint8_t subNicIndex = m_portMapping.getSubPortIndex(nic);
    return m_qpInfoScaleOut.at(comm).at(remoteRank).at(subNicIndex).at(qpSet).at(qpi);
}

uint32_t
QPManagerScaleOutGaudi2::getQPi(HCL_Comm comm, const uint8_t nic, const unsigned qpn, const HCL_Rank remoteRank)
{
    uint8_t subNicIndex = m_portMapping.getSubPortIndex(nic);

    for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
    {
        for (unsigned qpi = 0; qpi < MAX_QPS_PER_CONNECTION_G2; qpi++)
        {
            if (m_qpInfoScaleOut.at(comm).at(remoteRank).at(subNicIndex).at(qpSet).at(qpi) == qpn)
            {
                return qpi;
            }
        }
    }

    VERIFY(false, "could not find a match for comm {} rank {} subNic {} qpn {}", comm, remoteRank, subNicIndex, qpn);
    return 0;
}

void QPManagerScaleOutGaudi2::closeQPs(HCL_Comm comm, const UniqueSortedVector& ranks)
{
    for (auto& rank : ranks)
    {
        for (unsigned subNicIndex = 0; subNicIndex < COMPACT_RANK_INFO_NICS; subNicIndex++)
        {
            for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
            {
                for (unsigned qpi = 0; qpi < MAX_QPS_PER_CONNECTION_G2; qpi++)
                {
                    uint32_t qpn = m_qpInfoScaleOut.at(comm).at(rank).at(subNicIndex).at(qpSet).at(qpi);
                    if (isInvalidQPn(qpn)) continue;

                    unsigned nic = m_portMapping.getScaleoutNicFromSubPort(subNicIndex);
                    LOG_HCL_TRACE(HCL,
                                  "closing QP: comm({}) rank({}) nic({}) qpSet{} qpi({}) qpn({})",
                                  comm,
                                  rank,
                                  nic,
                                  qpSet,
                                  qpi,
                                  qpn);
                    m_device->destroyQp(nic, qpn);

                    m_qpInfoScaleOut.at(comm).at(rank).at(subNicIndex).at(qpSet).at(qpi) = 0;
                }
            }
        }
    }
}