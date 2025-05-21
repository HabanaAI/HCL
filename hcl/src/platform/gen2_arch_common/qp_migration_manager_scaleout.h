#pragma once

#include "hcl_types.h"
#include "qp_manager.h"
#include <cstdint>

struct migration_qp_data_t
{
    uint32_t qpn;
    uint16_t oldNic;
    uint16_t newNic;
};

typedef std::array<std::array<migration_qp_data_t, MAX_QPS_PER_CONNECTION_G3>, MAX_QPS_SETS_PER_CONNECTION>
    rank_qps_data_t;

class MigrationQpManager
{
public:
    MigrationQpManager(const uint16_t maxQPsPerConnection) : m_maxQPsPerConnection(maxQPsPerConnection) {};
    MigrationQpManager(MigrationQpManager&&)                 = delete;
    MigrationQpManager(const MigrationQpManager&)            = delete;
    MigrationQpManager& operator=(MigrationQpManager&&)      = delete;
    MigrationQpManager& operator=(const MigrationQpManager&) = delete;

    void     addMigrationQpsToQPManagerDB(const QPManagerHints& hints, const rank_qps_data_t qps);
    void     allocateMigrationQPDBStorage(const HCL_Comm comm, const uint16_t nic, uint32_t commSize);
    uint32_t getQPi(const QPManagerHints& hints, uint32_t nic2qpOffset = 0) const;
    uint32_t getQPn(const QPManagerHints& hints) const;
    uint16_t getNewNicSafe(const QPManagerHints& hints) const;
    uint16_t getNewNic(const QPManagerHints& hints) const;
    uint16_t getOldNic(const QPManagerHints& hints) const;
    void     releaseQpsResource(HclDeviceGen2Arch& device, const HCL_Comm comm, const UniqueSortedVector& outerRanks);
    void     resizeDBforNewNic(const HCL_Comm comm, const uint16_t nic, uint32_t commSize);
    uint16_t getOldNicIndex(HCL_Comm comm, uint16_t nic);

    const uint16_t getNumMigrationNics() const;
    const uint32_t getMaxQpsPerConnection() const;

private:
    // m_qpInfoScaleOut[nic][remoteRank][qpSet][qpi]
    std::vector<std::vector<
        std::array<std::array<migration_qp_data_t, MAX_QPS_PER_CONNECTION_G3>, MAX_QPS_SETS_PER_CONNECTION>>>
        m_qpInfoScaleOut;

    mutable std::mutex m_qpInfoScaleOutMutex;

    const uint16_t m_maxQPsPerConnection;
};
