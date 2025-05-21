#include "qp_manager_scaleout.h"

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
#include "platform/gen2_arch_common/server_def.h"

/* ScaleOut QP Manager*/

QPManagerGaudi3ScaleOut::QPManagerGaudi3ScaleOut(HclDeviceGaudi3& device) : QPManagerGaudi3(device) {}

void QPManagerGaudi3ScaleOut::resizeDBPerComm(size_t commSize)
{
    m_qpInfoScaleOut.resize(commSize);
    for (auto& qpSet : m_qpInfoScaleOut)
    {
        for (auto& qpi : qpSet)
        {
            qpi.fill(INVALID_QP);
        }
    }
}

void QPManagerGaudi3ScaleOut::addQPsToQPManagerDB(const QPManagerHints& hints, const QpsVector& qps)
{
    const HCL_Comm comm       = hints.m_comm;
    const unsigned remoteRank = hints.m_remoteRank;

    for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
    {
        for (unsigned qpi = 0; qpi < m_maxQPsPerConnection; qpi++)
        {
            const unsigned qpIndex = m_maxQPsPerConnection * qpSet + qpi;
            if (qpIndex >= qps.size()) break;

            const uint32_t qpBase = qps.at(qpIndex);

            m_qpInfoScaleOut.at(remoteRank).at(qpSet).at(qpi) = qpBase;

            LOG_HCL_DEBUG(HCL,
                          "m_qpInfoScaleOut[comm {}][rank {}][qpSet {}][qpi {}] = qpBase {}",
                          comm,
                          remoteRank,
                          qpSet,
                          qpi,
                          m_qpInfoScaleOut.at(remoteRank).at(qpSet).at(qpi));
        }
    }
}

uint32_t QPManagerGaudi3ScaleOut::getQPn(const QPManagerHints& hints) const
{
    const unsigned remoteRank = hints.m_remoteRank;
    const unsigned qpSet      = hints.m_qpSet;
    const unsigned qpi        = hints.m_qpi;

    return m_qpInfoScaleOut.at(remoteRank).at(qpSet).at(qpi);
}

uint32_t QPManagerGaudi3ScaleOut::getQPi(const QPManagerHints& hints) const
{
    const unsigned comm       = hints.m_comm;
    const unsigned remoteRank = hints.m_remoteRank;
    const unsigned nic        = hints.m_nic;
    const unsigned qpn        = hints.m_qpn;

    for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
    {
        for (unsigned qpi = 0; qpi < m_maxQPsPerConnection; qpi++)
        {
            if (m_qpInfoScaleOut.at(remoteRank).at(qpSet).at(qpi) + m_device.getNicToQpOffset(nic) == qpn)
            {
                return qpi;
            }
        }
    }

    VERIFY(false, "could not find a match for comm {} rank {} nic {} qpn {}", comm, remoteRank, nic, qpn);
    return 0;
}

void QPManagerGaudi3ScaleOut::ReleaseQPsResource(const QPManagerHints& hints)
{
    // in HNIC flows we do not open or register scaleout QPs, so do not need to close any
    if (m_qpInfoScaleOut.size() == 0) return;

    const HCL_Comm            comm  = hints.m_comm;
    const UniqueSortedVector& ranks = m_device.getComm(comm).getOuterRanksExclusive();

    m_device.getComm(comm).m_backupRankQPs.clear();  // delete backup QPs container

    for (auto& rank : ranks)
    {
        for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
        {
            for (unsigned qpi = 0; qpi < m_maxQPsPerConnection; qpi++)
            {
                for (auto nic : m_device.getActiveNics(m_device.getMyRank(comm), rank, 1, comm))
                {
                    if (!(m_device.isScaleOutPort(nic, comm))) continue;

                    const uint32_t qpBase = m_qpInfoScaleOut.at(rank).at(qpSet).at(qpi);
                    if (isInvalidQPn(qpBase)) continue;

                    const uint32_t qpn = qpBase + m_device.getNicToQpOffset(nic);
                    LOG_HCL_TRACE(HCL,
                                  "closing QP: comm({}) rank({}) nic({}) qpSet({}) qpi({}) qpn({})",
                                  comm,
                                  rank,
                                  nic,
                                  qpSet,
                                  qpi,
                                  qpn);

                    m_device.destroyQp(comm, nic, qpn);
                    m_device.getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs[nic].qp[qpSet][qpi] = INVALID_QP;
                }

                m_qpInfoScaleOut.at(rank).at(qpSet).at(qpi) = 0;
            }
        }
    }
}
