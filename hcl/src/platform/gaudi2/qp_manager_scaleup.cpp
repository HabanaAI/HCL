#include "qp_manager_scaleup.h"
#include "platform/gaudi2/qp_manager.h"
#include "platform/gaudi2/hcl_device.h"
#include "hcl_dynamic_communicator.h"
#include "hcl_utils.h"

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

void QPManagerGaudi2ScaleUp::addQPsToQPManagerDB(const QPManagerHints& hints, const QpsVector& qps)
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

void QPManagerGaudi2ScaleUp::ReleaseQPsResource(const QPManagerHints& hints)
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