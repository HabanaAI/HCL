#pragma once

#include <cstdint>          // for uint32_t, uint8_t
#include <array>            // for array
#include <set>              // for set
#include <vector>           // for vector
#include "hcl_api_types.h"  // for HCL_Comm
#include "hcl_dynamic_communicator.h"
#include "hcl_types.h"
#include "infra/scal/gen2_arch_common/scal_stream.h"
#include "platform/gaudi3/port_mapping.h"
#include "platform/gen2_arch_common/types.h"  // for QpInfo
#include "platform/gen2_arch_common/qp_manager.h"

// since we use collective qps in gaudi3, we use the same qp IDs for all ranks in scaleUp, so the ranks is irelevant in
// th DB. the same set of qp IDs are used throughout all scale up ports. but for scale out we use the same nics for all
// peers, so each rank gets a new set of qps. and they are saved separately in the DB

#define INVALID_COUNT ((uint64_t)-1)

constexpr unsigned MAX_QPS_PER_CONNECTION_G3 = 6;

enum G3QP_e
{
    QPE_RS_RECV = 0,
    QPE_AG_RECV,
    QPE_A2A_RECV,
    QPE_RS_SEND,
    QPE_AG_SEND,
    QPE_A2A_SEND
};

class HclDeviceGaudi3;

class QPUsage
{
public:
    uint32_t qpn;
    bool     disregardRank;
};

class QPManagerGaudi3 : public QPManager
{
public:
    QPManagerGaudi3() = delete;
    QPManagerGaudi3(HclDeviceGaudi3* device);
    virtual ~QPManagerGaudi3() = default;

    virtual void registerQPs(HCL_Comm comm, const QpsVector& qps, const HCL_Rank remoteRank, const unsigned qpSets) = 0;

    virtual uint32_t getQP(HCL_Comm comm, const unsigned qpi, const HCL_Rank remoteRank, const uint8_t qpSet) = 0;
    virtual uint32_t getQPi(HCL_Comm comm, const uint8_t nic, const uint32_t qpn, const HCL_Rank remoteRank)  = 0;

    virtual QPUsage getBaseQpAndUsage(HclDynamicCommunicator& dynamicComm,
                                      HCL_CollectiveOp        collectiveOp,
                                      bool                    isSend,
                                      bool                    isComplexCollective,
                                      bool                    isReductionInIMB,
                                      bool                    isHierarchical,
                                      uint64_t                count,
                                      uint64_t                cellCount,
                                      HclConfigType           boxType,
                                      bool                    isScaleOut        = false,
                                      HCL_Rank                remoteRank        = HCL_INVALID_RANK,
                                      uint8_t                 qpSet             = 0,
                                      const bool              isReproReduction  = false,
                                      HCL_CollectiveOp        complexCollective = eHCLNoCollective,
                                      bool                    isRoot            = false);

    virtual void closeQPs(HCL_Comm comm, const UniqueSortedVector& ranks) = 0;

protected:
    HclDeviceGaudi3* m_device = nullptr;

private:
    virtual void resizeDB(HCL_Comm comm) = 0;
};

class QPManagerScaleUpGaudi3 : public QPManagerGaudi3
{
public:
    QPManagerScaleUpGaudi3() = delete;
    QPManagerScaleUpGaudi3(HclDeviceGaudi3* device);
    virtual ~QPManagerScaleUpGaudi3() = default;

    void registerQPs(HCL_Comm         comm,
                     const QpsVector& qps,
                     const HCL_Rank   remoteRank = HCL_INVALID_RANK,
                     const unsigned   qpSets     = 0) override;
    void closeQPs(HCL_Comm comm, const UniqueSortedVector& ranks) override;

    uint32_t getQP(HCL_Comm       comm,
                   const unsigned qpi,
                   const HCL_Rank remoteRank = HCL_INVALID_RANK,
                   const uint8_t  qpSet      = 0) override;
    uint32_t
    getQPi(HCL_Comm comm, const uint8_t nic, const uint32_t qpn, HCL_Rank const remoteRank = HCL_INVALID_RANK) override;

    void setNicOffsets(hcl::ScalStream& Stream,
                       HclDeviceGaudi3* device,
                       HCL_Comm         comm,
                       HCL_CollectiveOp collectiveOp,
                       bool             isSend);

    void setLastRankScaleup(hcl::ScalStream& Stream,
                            HclDeviceGaudi3* device,
                            HCL_Comm         comm,
                            HCL_CollectiveOp collectiveOp,
                            bool             isSend);

protected:
    uint32_t getLastRankPortMask(HclDynamicCommunicator&  dynamicComm,
                                 HCL_CollectiveOp         collectiveOp,
                                 bool                     isSend,
                                 Gaudi3DevicePortMapping& portMapping);

private:
    void resizeDB(HCL_Comm comm) override;
    void resizeOffsetDBs(HCL_Comm comm);

    std::array<uint16_t, MAX_NICS_GEN2ARCH>& getRemoteRankIndices(HclDynamicCommunicator&  dynamicComm,
                                                                  HCL_CollectiveOp         collectiveOp,
                                                                  bool                     isSend,
                                                                  Gaudi3DevicePortMapping& portMapping,
                                                                  uint64_t                 nicsStatusMask,
                                                                  const uint64_t           maxNics);

    // m_qpInfoScaleUp[comm][qpi] -> qpn
    std::vector<std::array<qpn, MAX_QPS_PER_CONNECTION_G3>> m_qpInfoScaleUp;

    std::vector<std::array<uint16_t, MAX_NICS_GEN2ARCH>> m_remoteRankOffsets;
    std::vector<std::array<uint16_t, MAX_NICS_GEN2ARCH>> m_myRankOffsets;
};

class QPManagerScaleOutGaudi3 : public QPManagerGaudi3
{
public:
    QPManagerScaleOutGaudi3() = delete;
    QPManagerScaleOutGaudi3(HclDeviceGaudi3* device);
    virtual ~QPManagerScaleOutGaudi3() = default;

    void registerQPs(HCL_Comm comm, const QpsVector& qps, const HCL_Rank remoteRank, const unsigned qpSets) override;
    void closeQPs(HCL_Comm comm, const UniqueSortedVector& ranks) override;
    void allocateCommQPs(HCL_Comm comm, const uint32_t commSize);

    uint32_t getQP(HCL_Comm comm, const unsigned qpi, const HCL_Rank remoteRank, const uint8_t qpSet) override;
    uint32_t getQPi(HCL_Comm comm, const uint8_t nic, const uint32_t qpn, const HCL_Rank remoteRank) override;

    static inline bool isRsQp(unsigned index) { return (index == QPE_RS_RECV || index == QPE_RS_SEND); };
    static inline bool isA2AQp(unsigned index) { return (index == QPE_A2A_RECV || index == QPE_A2A_SEND); };

private:
    void resizeDB(HCL_Comm comm) override;
    void resizeDBForComm(HCL_Comm comm, const size_t commSize);

    // m_qpInfoScaleOut[comm][remoteRank][qpSet][qpi] -> qpn
    std::vector<std::vector<std::array<std::array<qpn, MAX_QPS_PER_CONNECTION_G3>, MAX_QPS_SETS_PER_CONNECTION>>>
        m_qpInfoScaleOut;
};