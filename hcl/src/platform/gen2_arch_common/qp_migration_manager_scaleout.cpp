#include "qp_migration_manager_scaleout.h"
#include "ibverbs/hcl_ibverbs.h"

void MigrationQpManager::addMigrationQpsToQPManagerDB(const QPManagerHints& hints, const rank_qps_data_t qps)
{
    const unsigned comm       = hints.m_comm;
    const unsigned remoteRank = hints.m_remoteRank;
    const unsigned nic        = hints.m_nic;

    const uint16_t nicIndex = getOldNicIndex(comm, nic);

    std::lock_guard<std::mutex> lock(m_qpInfoScaleOutMutex);
    m_qpInfoScaleOut[nicIndex][remoteRank] = qps;
}

void MigrationQpManager::allocateMigrationQPDBStorage(const HCL_Comm comm, const uint16_t nic, uint32_t commSize)
{
    std::lock_guard<std::mutex> lock(m_qpInfoScaleOutMutex);
    resizeDBforNewNic(comm, nic, commSize);
}

uint32_t MigrationQpManager::getQPi(const QPManagerHints& hints, uint32_t nic2qpOffset) const
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
            if (m_qpInfoScaleOut[nic][remoteRank][qpSet][qpi].qpn + nic2qpOffset == qpn)
            {
                return qpi;
            }
        }
    }
    VERIFY(false, "could not find a match for comm {} rank {} nic {} qpn {}", comm, remoteRank, nic, qpn);
}

uint32_t MigrationQpManager::getQPn(const QPManagerHints& hints) const
{
    std::lock_guard<std::mutex> lock(m_qpInfoScaleOutMutex);
    return m_qpInfoScaleOut[hints.m_nic][hints.m_remoteRank][hints.m_qpSet][hints.m_qpi].qpn;
}

uint16_t MigrationQpManager::getOldNic(const QPManagerHints& hints) const
{
    std::lock_guard<std::mutex> lock(m_qpInfoScaleOutMutex);
    return m_qpInfoScaleOut[hints.m_nic][hints.m_remoteRank][hints.m_qpSet][hints.m_qpi].oldNic;
}

uint16_t MigrationQpManager::getNewNicSafe(const QPManagerHints& hints) const
{
    return m_qpInfoScaleOut[hints.m_nic][hints.m_remoteRank][hints.m_qpSet][hints.m_qpi].newNic;
}

uint16_t MigrationQpManager::getNewNic(const QPManagerHints& hints) const
{
    std::lock_guard<std::mutex> lock(m_qpInfoScaleOutMutex);
    return getNewNicSafe(hints);
}

void MigrationQpManager::releaseQpsResource(HclDeviceGen2Arch&        device,
                                            const HCL_Comm            comm,
                                            const UniqueSortedVector& outerRanks)
{
    LOG_HCL_INFO(HCL, "Going to delete migration QPs");

    std::lock_guard<std::mutex> lock(m_qpInfoScaleOutMutex);

    if (m_qpInfoScaleOut.size() == 0)
    {
        LOG_HCL_TRACE(HCL, "no migration QPs - exit");
        return;
    }

    for (auto& rank : outerRanks)
    {
        for (uint16_t nicIndex = 0; nicIndex < m_qpInfoScaleOut.size(); nicIndex++)
        {
            uint16_t       nic       = getNewNicSafe(QPManagerHints(comm, rank, nicIndex, 0, 0));
            const uint32_t nicOffset = device.getNicToQpOffset(nic);
            for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
            {
                for (unsigned qpi = 0; qpi < m_maxQPsPerConnection; qpi++)
                {
                    LOG_HCL_TRACE(HCL, "comm {} nicIndex {} rank {} qpSet {} qpi {}", comm, nicIndex, rank, qpSet, qpi);
                    if (m_qpInfoScaleOut[nicIndex][rank][qpSet][qpi].qpn != INVALID_QP)
                    {
                        const uint32_t qpn = m_qpInfoScaleOut[nicIndex][rank][qpSet][qpi].qpn;
                        LOG_HCL_TRACE(HCL, "qpn {} nicOffset {}", qpn, nicOffset);

                        g_ibv.destroy_qp(comm, m_qpInfoScaleOut[nicIndex][rank][qpSet][qpi].newNic, qpn);
                        m_qpInfoScaleOut[nicIndex][rank][qpSet][qpi].qpn = INVALID_QP;
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
    m_qpInfoScaleOut.clear();  // released all resources - free mem
}

void MigrationQpManager::resizeDBforNewNic(const HCL_Comm comm, const uint16_t nic, uint32_t commSize)
{
    uint16_t nicIndex;
    uint16_t numNics = m_qpInfoScaleOut.size();

    LOG_HCL_DEBUG(HCL, "resizing comm {} for nic {}", comm, nic);

    for (nicIndex = 0; nicIndex < numNics; nicIndex++)
    {
        if (nic == m_qpInfoScaleOut[nicIndex][0][0][0].oldNic)
        {
            return;  // found nic container - exit
        }
    }
    // not found - add container
    numNics++;
    m_qpInfoScaleOut.resize(numNics);
    m_qpInfoScaleOut[numNics - 1].resize(commSize);
    LOG_HCL_DEBUG(HCL, "not found. new size {} for comm {} nic {}", numNics, comm, nic);
    for (unsigned rank = 0; rank < commSize; rank++)
    {
        for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
        {
            for (unsigned qpi = 0; qpi < MAX_QPS_PER_CONNECTION_G3; qpi++)
            {
                m_qpInfoScaleOut[nicIndex][rank][qpSet][qpi].oldNic = nic;
            }
        }
    }
}

const uint16_t MigrationQpManager::getNumMigrationNics() const
{
    std::lock_guard<std::mutex> lock(m_qpInfoScaleOutMutex);
    return m_qpInfoScaleOut.size();
}

const uint32_t MigrationQpManager::getMaxQpsPerConnection() const
{
    return m_maxQPsPerConnection;
}

uint16_t MigrationQpManager::getOldNicIndex(HCL_Comm comm, uint16_t nic)
{
    uint16_t nicIndex;

    std::lock_guard<std::mutex> lock(m_qpInfoScaleOutMutex);

    for (nicIndex = 0; nicIndex < m_qpInfoScaleOut.size(); nicIndex++)
    {
        if (nic == m_qpInfoScaleOut[nicIndex][0][0][0].oldNic)
        {
            return nicIndex;
        }
    }
    VERIFY(false, "comm {}: nic {} not found", comm, nic);
}
