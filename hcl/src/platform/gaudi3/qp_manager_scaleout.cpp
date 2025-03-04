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

void QPManagerGaudi3ScaleOut::resizeDBForNewComms(const HCL_Comm comm)
{
    const size_t oldSize = m_qpInfoScaleOut.size();
    const size_t newSize = oldSize + DEFAULT_COMMUNICATORS_SIZE;

    LOG_HCL_INFO(HCL, "resizing m_qpInfoScaleOut for comm {} from {} to {}", comm, oldSize, newSize);

    m_qpInfoScaleOut.resize(newSize);
    for (unsigned index = oldSize; index < newSize; index++)
    {
        for (auto& qpSet : m_qpInfoScaleOut.at(index))
        {
            for (auto& qpi : qpSet)
            {
                qpi.fill(INVALID_QP);
            }
        }
    }
}

void QPManagerGaudi3ScaleOut::resizeDBPerComm(const HCL_Comm comm)
{
    const size_t commSize = m_device.getCommSize(comm);

    LOG_HCL_INFO(HCL, "resizing for comm {} to size {}", comm, commSize);

    m_qpInfoScaleOut.at(comm).resize(commSize);
    for (auto& qpSet : m_qpInfoScaleOut.at(comm))
    {
        for (auto& qpi : qpSet)
        {
            qpi.fill(INVALID_QP);
        }
    }
}

void QPManagerGaudi3ScaleOut::allocateQPDBStorage(const HCL_Comm comm)
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

void QPManagerGaudi3ScaleOut::addQPsToQPManagerDB(const QPManagerHints& hints, const QpsVector& qps)
{
    const HCL_Comm comm       = hints.m_comm;
    const unsigned remoteRank = hints.m_remoteRank;

    if (unlikely(comm >= m_qpInfoScaleOut.size()))
    {
        resizeDBForNewComms(comm);
    }
    if (unlikely(m_qpInfoScaleOut.at(comm).size() == 0))
    {
        resizeDBPerComm(comm);
    }

    for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
    {
        for (unsigned qpi = 0; qpi < m_maxQPsPerConnection; qpi++)
        {
            const unsigned qpIndex = m_maxQPsPerConnection * qpSet + qpi;
            if (qpIndex >= qps.size()) break;

            const uint32_t qpBase = qps.at(qpIndex);

            m_qpInfoScaleOut.at(comm).at(remoteRank).at(qpSet).at(qpi) = qpBase;

            LOG_HCL_DEBUG(HCL,
                          "m_qpInfoScaleOut[comm {}][rank {}][qpSet {}][qpi {}] = qpBase {}",
                          comm,
                          remoteRank,
                          qpSet,
                          qpi,
                          m_qpInfoScaleOut.at(comm).at(remoteRank).at(qpSet).at(qpi));
        }
    }
}

uint32_t QPManagerGaudi3ScaleOut::getQPn(const QPManagerHints& hints) const
{
    const HCL_Comm comm       = hints.m_comm;
    const unsigned remoteRank = hints.m_remoteRank;
    const unsigned qpSet      = hints.m_qpSet;
    const unsigned qpi        = hints.m_qpi;

    return m_qpInfoScaleOut.at(comm).at(remoteRank).at(qpSet).at(qpi);
}

uint32_t QPManagerGaudi3ScaleOut::getQPi(const QPManagerHints& hints) const
{
    const HCL_Comm comm       = hints.m_comm;
    const unsigned remoteRank = hints.m_remoteRank;
    const unsigned nic        = hints.m_nic;
    const unsigned qpn        = hints.m_qpn;

    for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
    {
        for (unsigned qpi = 0; qpi < m_maxQPsPerConnection; qpi++)
        {
            if (m_qpInfoScaleOut.at(comm).at(remoteRank).at(qpSet).at(qpi) + m_device.getNicToQpOffset(nic) == qpn)
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
    const HCL_Comm            comm  = hints.m_comm;
    const UniqueSortedVector& ranks = m_device.getComm(comm).getOuterRanksExclusive();

    // in HNIC flows we do not open or register scaleout QPs, so do not need to close any
    if (m_qpInfoScaleOut.size() == 0) return;

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

                    const uint32_t qpBase = m_qpInfoScaleOut.at(comm).at(rank).at(qpSet).at(qpi);
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

                    m_device.destroyQp(nic, qpn);
                    m_device.getComm(comm).m_rankInfo.remoteInfo[rank].gaudiNicQPs[nic].qp[qpSet][qpi] = INVALID_QP;
                }

                m_qpInfoScaleOut.at(comm).at(rank).at(qpSet).at(qpi) = 0;
            }
        }
    }
}

uint16_t MigrationScaleOutQpManagerGaudi3::getOldNicIndex(HCL_Comm comm, uint16_t nic)
{
    uint16_t nicIndex;

    std::lock_guard<std::mutex> lock(m_qpInfoScaleOutMutex);

    for (nicIndex = 0; nicIndex < m_qpInfoScaleOut[comm].size(); nicIndex++)
    {
        if (nic == m_qpInfoScaleOut[comm][nicIndex][0][0][0].oldNic)
        {
            return nicIndex;
        }
    }
    VERIFY(false, "comm {}: nic {} not found", comm, nic);
}

void MigrationScaleOutQpManagerGaudi3::addMigrationQpsToQPManagerDB(const QPManagerHints& hints,
                                                                    const rank_qps_data_t qps)
{
    const HCL_Comm comm       = hints.m_comm;
    const unsigned remoteRank = hints.m_remoteRank;
    const unsigned nic        = hints.m_nic;

    const uint16_t nicIndex = getOldNicIndex(comm, nic);

    std::lock_guard<std::mutex> lock(m_qpInfoScaleOutMutex);
    m_qpInfoScaleOut[comm][nicIndex][remoteRank] = qps;
}

void MigrationScaleOutQpManagerGaudi3::allocateMigrationQPDBStorage(const HCL_Comm comm,
                                                                    const uint16_t nic,
                                                                    uint32_t       commSize)
{
    std::lock_guard<std::mutex> lock(m_qpInfoScaleOutMutex);
    if (comm >= m_qpInfoScaleOut.size())
    {
        resizeDBForNewComm(comm);
    }
    resizeDBforNewNic(comm, nic, commSize);
}

uint32_t MigrationScaleOutQpManagerGaudi3::getQPi(const QPManagerHints& hints, uint32_t nic2qpOffset) const
{
    const HCL_Comm comm       = hints.m_comm;
    const unsigned remoteRank = hints.m_remoteRank;
    const unsigned nic        = hints.m_nic;
    const unsigned qpn        = hints.m_qpn;

    std::lock_guard<std::mutex> lock(m_qpInfoScaleOutMutex);

    for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
    {
        for (unsigned qpi = 0; qpi < m_maxQPsPerConnection; qpi++)
        {
            if (m_qpInfoScaleOut[comm][nic][remoteRank][qpSet][qpi].qpn + nic2qpOffset == qpn)
            {
                return qpi;
            }
        }
    }
    VERIFY(false, "could not find a match for comm {} rank {} nic {} qpn {}", comm, remoteRank, nic, qpn);
}

uint32_t MigrationScaleOutQpManagerGaudi3::getQPn(const QPManagerHints& hints) const
{
    std::lock_guard<std::mutex> lock(m_qpInfoScaleOutMutex);
    return m_qpInfoScaleOut[hints.m_comm][hints.m_nic][hints.m_remoteRank][hints.m_qpSet][hints.m_qpi].qpn;
}

uint16_t MigrationScaleOutQpManagerGaudi3::getOldNic(const QPManagerHints& hints) const
{
    std::lock_guard<std::mutex> lock(m_qpInfoScaleOutMutex);
    return m_qpInfoScaleOut[hints.m_comm][hints.m_nic][hints.m_remoteRank][hints.m_qpSet][hints.m_qpi].oldNic;
}

uint16_t MigrationScaleOutQpManagerGaudi3::getNewNicSafe(const QPManagerHints& hints) const
{
    return m_qpInfoScaleOut[hints.m_comm][hints.m_nic][hints.m_remoteRank][hints.m_qpSet][hints.m_qpi].newNic;
}

uint16_t MigrationScaleOutQpManagerGaudi3::getNewNic(const QPManagerHints& hints) const
{
    std::lock_guard<std::mutex> lock(m_qpInfoScaleOutMutex);
    return getNewNicSafe(hints);
}

void MigrationScaleOutQpManagerGaudi3::releaseQpsResource(HclDeviceGen2Arch&        device,
                                                          const HCL_Comm            comm,
                                                          const UniqueSortedVector& outerRanks)
{
    LOG_HCL_INFO(HCL, "Going to delete migration QPs");

    // printf("Going to delete migration QPs\n");

    std::lock_guard<std::mutex> lock(m_qpInfoScaleOutMutex);

    if (m_qpInfoScaleOut.size() == 0)
    {
        LOG_HCL_TRACE(HCL, "no migration QPs - exit");
        return;
    }

    for (auto& rank : outerRanks)
    {
        for (uint16_t nicIndex = 0; nicIndex < m_qpInfoScaleOut[comm].size(); nicIndex++)
        {
            uint16_t       nic       = getNewNicSafe(QPManagerHints(comm, rank, nicIndex, 0, 0));
            const uint32_t nicOffset = device.getNicToQpOffset(nic);
            for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
            {
                for (unsigned qpi = 0; qpi < m_maxQPsPerConnection; qpi++)
                {
                    LOG_HCL_TRACE(HCL, "comm {} nicIndex {} rank{} qpSet {} qpi {}", comm, nicIndex, rank, qpSet, qpi);
                    if (m_qpInfoScaleOut[comm][nicIndex][rank][qpSet][qpi].qpn != INVALID_QP)
                    {
                        const uint32_t qpn = m_qpInfoScaleOut[comm][nicIndex][rank][qpSet][qpi].qpn;
                        LOG_HCL_TRACE(HCL, "qpn {} nicOffset {}", qpn, nicOffset);

                        g_ibv.destroy_qp(m_qpInfoScaleOut[comm][nicIndex][rank][qpSet][qpi].newNic, qpn);
                        m_qpInfoScaleOut[comm][nicIndex][rank][qpSet][qpi].qpn = INVALID_QP;
                        LOG_HCL_TRACE(HCL,
                                      "closed QP: comm {} nic {} index {} rank{} qpSet {} qpi {} qpn {}",
                                      comm,
                                      nic,
                                      nicIndex,
                                      rank,
                                      qpSet,
                                      qpi,
                                      qpn);
                    }
                }
            }
        }
    }
    m_qpInfoScaleOut[comm].clear();  // released all resources - free mem
}

void MigrationScaleOutQpManagerGaudi3::resizeDBForNewComm(const HCL_Comm comm)
{
    const size_t oldSize = m_qpInfoScaleOut.size();
    const size_t newSize = oldSize + DEFAULT_COMMUNICATORS_SIZE;

    LOG_HCL_INFO(HCL, "resizing m_qpInfoScaleOut for comm {} from {} to {}", comm, oldSize, newSize);

    m_qpInfoScaleOut.resize(newSize);
}

void MigrationScaleOutQpManagerGaudi3::resizeDBforNewNic(const HCL_Comm comm, const uint16_t nic, uint32_t commSize)
{
    uint16_t nicIndex;
    uint16_t numNics = m_qpInfoScaleOut[comm].size();

    LOG_HCL_DEBUG(HCL, "resizing comm {} for nic {}", comm, nic);

    for (nicIndex = 0; nicIndex < numNics; nicIndex++)
    {
        if (nic == m_qpInfoScaleOut[comm][nicIndex][0][0][0].oldNic)
        {
            return;  // found nic container - exit
        }
    }
    // not found - add container
    numNics++;
    m_qpInfoScaleOut[comm].resize(numNics);
    m_qpInfoScaleOut[comm][numNics - 1].resize(commSize);
    LOG_HCL_DEBUG(HCL, "not found. new size {} for comm {} nic {}", numNics, comm, nic);
    for (unsigned rank = 0; rank < commSize; rank++)
    {
        for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
        {
            for (unsigned qpi = 0; qpi < MAX_QPS_PER_CONNECTION_G3; qpi++)
            {
                m_qpInfoScaleOut[comm][nicIndex][rank][qpSet][qpi].oldNic = nic;
            }
        }
    }
}

const uint16_t MigrationScaleOutQpManagerGaudi3::getNumMigrationNics(const HCL_Comm comm) const
{
    std::lock_guard<std::mutex> lock(m_qpInfoScaleOutMutex);
    return m_qpInfoScaleOut[comm].size();
}

const uint32_t MigrationScaleOutQpManagerGaudi3::getMaxQpsPerConnection() const
{
    return m_maxQPsPerConnection;
}
