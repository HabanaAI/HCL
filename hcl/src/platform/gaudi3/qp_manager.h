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

// since we use collective qps in gaudi3, we use the same qp IDs for all ranks in scaleUp, so the ranks is irelevant in
// th DB. the same set of qp IDs are used throughout all scale up ports. but for scale out we use the same nics for all
// peers, so each rank gets a new set of qps. and they are saved separately in the DB

#define INVALID_COUNT       ((uint64_t)-1)
#define INVALID_RANK_OFFSET ((uint16_t)-1)

constexpr size_t MAX_QPS_PER_NIC_G3 = 6;

class HclDeviceGaudi3;

enum G3QP_e
{
    QPE_RS_RECV = 0,
    QPE_AG_RECV,
    QPE_A2A_RECV,
    QPE_RS_SEND,
    QPE_AG_SEND,
    QPE_A2A_SEND
};

class QPUsage
{
public:
    uint32_t qpn;
    bool     disregardRank;
};

class QPManager
{
public:
    QPManager() = delete;
    QPManager(HclDeviceGaudi3* device);
    virtual ~QPManager() = default;

    virtual bool isScaleUp() const = 0;
    virtual QpInfo* getQpInfo(HCL_Comm         comm,
                              HCL_CollectiveOp collectiveOp,
                              bool             isSend,
                              HCL_Rank         remoteRank = HCL_INVALID_RANK,
                              uint8_t          qpSet      = 0) = 0;
    virtual QpInfo*
    getQpInfo(HCL_Comm comm, unsigned index, HCL_Rank remoteRank = HCL_INVALID_RANK, uint8_t qpSet = 0) = 0;

    virtual void registerQPs(HCL_Comm comm, const QpsVector& qps, HCL_Rank remoteRank, uint8_t qpSets);
    virtual void registerQPs(HCL_Comm comm, const QpsVector& qps, HCL_Rank remoteRank = HCL_INVALID_RANK) = 0;
    uint32_t
    getBaseQp(HCL_Comm comm, HCL_CollectiveOp collectiveOp, bool isSend, HCL_Rank remoteRank = HCL_INVALID_RANK);
    virtual uint32_t getQpi(HCL_Comm comm, uint8_t nic, uint32_t qpn, HCL_Rank remoteRank = HCL_INVALID_RANK);
    virtual QpInfo*  seek(HCL_Comm comm, uint8_t nic, uint32_t qpn, HCL_Rank remoteRank = HCL_INVALID_RANK);

    uint32_t     getLastRankPortMask(HclDynamicCommunicator&  dynamicComm,
                                     HCL_CollectiveOp         collectiveOp,
                                     bool                     isSend,
                                     Gaudi3DevicePortMapping& portMapping);
    virtual void setNicOffsets(hcl::ScalStream& Stream,
                               HclDeviceGaudi3* device,
                               HCL_Comm         comm,
                               HCL_CollectiveOp collectiveOp,
                               bool             isSend);

    virtual std::array<uint16_t, MAX_NICS_GEN2ARCH>& getRemoteRankIndices(HclDynamicCommunicator&  dynamicComm,
                                                                          HCL_CollectiveOp         collectiveOp,
                                                                          bool                     isSend,
                                                                          Gaudi3DevicePortMapping& portMapping,
                                                                          uint64_t                 nicsStatusMask,
                                                                          const uint64_t           maxNics) = 0;

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

protected:
    virtual void     resizeDb()        = 0;
    virtual size_t   getDBSize() const = 0;
    HclDeviceGaudi3* m_device          = nullptr;
};

class QPManagerScaleUp : public QPManager
{
public:
    QPManagerScaleUp() = delete;
    QPManagerScaleUp(HclDeviceGaudi3* device);
    virtual ~QPManagerScaleUp() = default;

    virtual bool    isScaleUp() const override;
    virtual QpInfo* getQpInfo(HCL_Comm         comm,
                              HCL_CollectiveOp collectiveOp,
                              bool             isSend,
                              HCL_Rank         remoteRank = HCL_INVALID_RANK,
                              uint8_t          qpSet      = 0) override;
    virtual QpInfo*
    getQpInfo(HCL_Comm comm, unsigned index, HCL_Rank remoteRank = HCL_INVALID_RANK, uint8_t qpSet = 0) override;

    virtual void                                     setLastRankScaleup(hcl::ScalStream& Stream,
                                                                        HclDeviceGaudi3* device,
                                                                        HCL_Comm         comm,
                                                                        HCL_CollectiveOp collectiveOp,
                                                                        bool             isSend);
    virtual std::array<uint16_t, MAX_NICS_GEN2ARCH>& getRemoteRankIndices(HclDynamicCommunicator&  dynamicComm,
                                                                          HCL_CollectiveOp         collectiveOp,
                                                                          bool                     isSend,
                                                                          Gaudi3DevicePortMapping& portMapping,
                                                                          uint64_t                 nicsStatusMask,
                                                                          const uint64_t           maxNics) override;

    virtual void registerQPs(HCL_Comm comm, const QpsVector& qps, HCL_Rank remoteRank = HCL_INVALID_RANK) override;

protected:
    virtual void   resizeDb() override;
    virtual size_t getDBSize() const override;

private:
    void resizeOffsetsDB(HCL_Comm comm);
    // m_qpInfoDb[comm][qp] ->QpInfo
    std::vector<std::array<QpInfo, MAX_QPS_PER_NIC_G3>> m_qpInfoDb;

    std::vector<std::array<uint16_t, MAX_NICS_GEN2ARCH>> m_remoteRankOffsets;
    std::vector<std::array<uint16_t, MAX_NICS_GEN2ARCH>> m_myRankOffsets;
};

class QPManagerScaleOut : public QPManager
{
public:
    QPManagerScaleOut() = delete;
    QPManagerScaleOut(HclDeviceGaudi3* device);
    virtual ~QPManagerScaleOut() = default;

    virtual bool    isScaleUp() const override;
    virtual QpInfo* getQpInfo(HCL_Comm         comm,
                              HCL_CollectiveOp collectiveOp,
                              bool             isSend,
                              HCL_Rank         remoteRank = HCL_INVALID_RANK,
                              uint8_t          qpSet      = 0) override;
    virtual QpInfo*
    getQpInfo(HCL_Comm comm, unsigned index, HCL_Rank remoteRank = HCL_INVALID_RANK, uint8_t qpSet = 0) override;

    virtual std::array<uint16_t, MAX_NICS_GEN2ARCH>& getRemoteRankIndices(HclDynamicCommunicator&  dynamicComm,
                                                                          HCL_CollectiveOp         collectiveOp,
                                                                          bool                     isSend,
                                                                          Gaudi3DevicePortMapping& portMapping,
                                                                          uint64_t                 nicsStatusMask,
                                                                          const uint64_t           maxNics) override;

    void allocateCommQPs(HCL_Comm comm, uint32_t comm_size);

    static inline bool isRsQp(unsigned index) {return (index == QPE_RS_RECV || index == QPE_RS_SEND);};

    virtual void registerQPs(HCL_Comm comm, const QpsVector& qps, HCL_Rank remoteRank = HCL_INVALID_RANK) override;

protected:
    virtual void   resizeDb() override;
    virtual size_t getDBSize() const override;

private:
    // m_qpInfoDb[comm][rank][qpSet][qp] ->QpInfo
    std::vector<std::vector<std::array<std::array<QpInfo, MAX_QPS_PER_NIC_G3>, MAX_QPS_SETS_PER_CONNECTION>>>
        m_qpInfoDb;
};