#pragma once

#include "qp_manager.h"

class HclDeviceGaudi3;

class QPManagerGaudi3ScaleOut : public QPManagerGaudi3
{
public:
    QPManagerGaudi3ScaleOut(HclDeviceGaudi3& device);
    virtual ~QPManagerGaudi3ScaleOut() = default;

    virtual void addQPsToQPManagerDB(const QPManagerHints& hints, const QpsVector& qps) override;
    virtual void allocateQPDBStorage(const HCL_Comm comm) override;
    virtual void ReleaseQPsResource(const QPManagerHints& hints) override;

    virtual uint32_t getQPn(const QPManagerHints& hints) const override;
    virtual uint32_t getQPi(const QPManagerHints& hints) const override;

    static inline bool isRsQp(const unsigned index)
    {
        return (index == G3::QP_e::QPE_RS_RECV || index == G3::QP_e::QPE_RS_SEND);
    };
    static inline bool isA2AQp(const unsigned index)
    {
        return (index == G3::QP_e::QPE_A2A_RECV || index == G3::QP_e::QPE_A2A_SEND);
    };

private:
    void resizeDBForNewComms(const HCL_Comm comm);
    void resizeDBPerComm(const HCL_Comm comm);

    // m_qpInfoScaleOut[comm][remoteRank][qpSet][qpi] -> qpn
    std::vector<std::vector<std::array<std::array<QPn, MAX_QPS_PER_CONNECTION_G3>, MAX_QPS_SETS_PER_CONNECTION>>>
        m_qpInfoScaleOut;
};

struct migration_qp_data_t
{
    uint32_t qpn;
    uint16_t oldNic;
    uint16_t newNic;
};

typedef std::array<std::array<migration_qp_data_t, MAX_QPS_PER_CONNECTION_G3>, MAX_QPS_SETS_PER_CONNECTION>
    rank_qps_data_t;

class MigrationScaleOutQpManagerGaudi3
{
public:
    MigrationScaleOutQpManagerGaudi3(const uint16_t maxQPsPerConnection)
    : m_maxQPsPerConnection(maxQPsPerConnection) {};
    MigrationScaleOutQpManagerGaudi3(MigrationScaleOutQpManagerGaudi3&&)                 = delete;
    MigrationScaleOutQpManagerGaudi3(const MigrationScaleOutQpManagerGaudi3&)            = delete;
    MigrationScaleOutQpManagerGaudi3& operator=(MigrationScaleOutQpManagerGaudi3&&)      = delete;
    MigrationScaleOutQpManagerGaudi3& operator=(const MigrationScaleOutQpManagerGaudi3&) = delete;

    void     addMigrationQpsToQPManagerDB(const QPManagerHints& hints, const rank_qps_data_t qps);
    void     allocateMigrationQPDBStorage(const HCL_Comm comm, const uint16_t nic, uint32_t commSize);
    uint32_t getQPi(const QPManagerHints& hints, uint32_t nic2qpOffset = 0) const;
    uint32_t getQPn(const QPManagerHints& hints) const;
    uint16_t getNewNicSafe(const QPManagerHints& hints) const;
    uint16_t getNewNic(const QPManagerHints& hints) const;
    uint16_t getOldNic(const QPManagerHints& hints) const;
    void     releaseQpsResource(HclDeviceGen2Arch& device, const HCL_Comm comm, const UniqueSortedVector& outerRanks);
    void     resizeDBForNewComm(const HCL_Comm comm);
    void     resizeDBforNewNic(const HCL_Comm comm, const uint16_t nic, uint32_t commSize);
    uint16_t getOldNicIndex(HCL_Comm comm, uint16_t nic);

    const uint16_t getNumMigrationNics(const HCL_Comm comm) const;
    const uint32_t getMaxQpsPerConnection() const;

private:
    // m_qpInfoScaleOut[comm][nic][remoteRank][qpSet][qpi]
    std::vector<std::vector<std::vector<
        std::array<std::array<migration_qp_data_t, MAX_QPS_PER_CONNECTION_G3>, MAX_QPS_SETS_PER_CONNECTION>>>>
        m_qpInfoScaleOut;

    mutable std::mutex m_qpInfoScaleOutMutex;

    const uint16_t m_maxQPsPerConnection;
};
