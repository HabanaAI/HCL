#pragma once

#include "platform/gen2_arch_common/qp_manager.h"
#include "platform/gen2_arch_common/types.h"
#include "platform/gaudi2/port_mapping.h"
#include "hcl_types.h"

#include <array>
#include <vector>

constexpr unsigned MAX_QPS_PER_CONNECTION_G2 = 4;

class HclDeviceGaudi2;

class QPManagerGaudi2 : public QPManager
{
public:
    QPManagerGaudi2(HclDeviceGaudi2* device);
    virtual ~QPManagerGaudi2() = default;

    virtual void registerQPs(HCL_Comm         comm,
                             const uint8_t    nic,
                             const QpsVector& qps,
                             const HCL_Rank   remoteRank,
                             const uint32_t   commSize,
                             const unsigned   qpSets) = 0;

    virtual uint32_t
    getQP(HCL_Comm comm, const uint8_t nic, const unsigned qpi, const uint8_t qpSet, const HCL_Rank remoteRank) = 0;
    virtual uint32_t getQPi(HCL_Comm comm, const uint8_t nic, const unsigned qpn, const HCL_Rank remoteRank)    = 0;

    virtual void closeQPs(HCL_Comm comm, const UniqueSortedVector& ranks) override = 0;

protected:
    virtual void resizeDB(HCL_Comm comm) = 0;

    HclDeviceGaudi2* m_device = nullptr;
};

class QPManagerScaleUpGaudi2 : QPManagerGaudi2
{
public:
    QPManagerScaleUpGaudi2(HclDeviceGaudi2* device);
    virtual ~QPManagerScaleUpGaudi2() = default;

    void registerQPs(HCL_Comm         comm,
                     const uint8_t    nic,
                     const QpsVector& qps,
                     const HCL_Rank   remoteRank = HCL_INVALID_RANK,
                     const uint32_t   commSize   = 0,
                     const unsigned   qpSets     = 0) override;
    void closeQPs(HCL_Comm comm, const UniqueSortedVector& ranks) override;

    uint32_t getQP(HCL_Comm       comm,
                   const uint8_t  nic,
                   const unsigned qpi,
                   const uint8_t  qpSet      = 0,
                   const HCL_Rank remoteRank = HCL_INVALID_RANK);
    uint32_t getQPi(HCL_Comm comm, const uint8_t nic, const unsigned qpn, const HCL_Rank remoteRank = HCL_INVALID_RANK);

protected:
    void resizeDB(HCL_Comm comm) override;

private:
    // m_qpInfoScaleUp[comm][nic][qpi] -> qpn
    std::vector<std::array<std::array<qpn, MAX_QPS_PER_CONNECTION_G2>, MAX_NICS_GEN2ARCH>> m_qpInfoScaleUp;
};

class QPManagerScaleOutGaudi2 : QPManagerGaudi2
{
public:
    QPManagerScaleOutGaudi2(HclDeviceGaudi2* device, Gaudi2DevicePortMapping& portMapping);
    virtual ~QPManagerScaleOutGaudi2() = default;

    void registerQPs(HCL_Comm         comm,
                     const uint8_t    nic,
                     const QpsVector& qps,
                     const HCL_Rank   remoteRank,
                     const uint32_t   commSize,
                     const unsigned   qpSets) override;
    void closeQPs(HCL_Comm comm, const UniqueSortedVector& ranks) override;

    void allocateCommQPs(HCL_Comm comm, const uint32_t commSize);

    uint32_t
    getQP(HCL_Comm comm, const uint8_t nic, const unsigned qpi, const uint8_t qpSet, const HCL_Rank remoteRank);
    uint32_t getQPi(HCL_Comm comm, const uint8_t nic, const unsigned qpn, const HCL_Rank remoteRank);

protected:
    void resizeDB(HCL_Comm comm) override;
    void resizeDBForComm(HCL_Comm comm, const uint32_t commSize);

private:
    Gaudi2DevicePortMapping& m_portMapping;

    // m_qpInfoScaleOut[comm][remoteRank][subNicIndex][qpSet][qpi] -> qpn
    std::vector<
        std::vector<std::array<std::array<std::array<qpn, MAX_QPS_PER_CONNECTION_G2>, MAX_QPS_SETS_PER_CONNECTION>,
                               COMPACT_RANK_INFO_NICS>>>
        m_qpInfoScaleOut;
};

using QPManagerScaleUpGaudi2Handle  = std::unique_ptr<QPManagerScaleUpGaudi2>;
using QPManagerScaleOutGaudi2Handle = std::unique_ptr<QPManagerScaleOutGaudi2>;