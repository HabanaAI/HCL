#pragma once

#include <cstdint>          // for uint32_t, uint8_t
#include <array>            // for array
#include <set>              // for set
#include <vector>           // for vector
#include "hcl_api_types.h"  // for HCL_Comm
#include "hcl_dynamic_communicator.h"
#include "hcl_types.h"
#include "infra/scal/gen2_arch_common/scal_stream.h"
#include "platform/gen2_arch_common/qp_manager.h"

// since we use collective qps in gaudi3, we use the same qp IDs for all ranks in scaleUp, so the ranks is irelevant in
// th DB. the same set of qp IDs are used throughout all scale up ports. but for scale out we use the same nics for all
// peers, so each rank gets a new set of qps. and they are saved separately in the DB

#define INVALID_COUNT ((uint64_t) - 1)

constexpr unsigned MAX_QPS_PER_CONNECTION_G3 = 6;

namespace G3
{
enum QP_e
{
    QPE_RS_RECV = 0,
    QPE_AG_RECV,
    QPE_A2A_RECV,
    QPE_RS_SEND,
    QPE_AG_SEND,
    QPE_A2A_SEND
};
}

class HclDeviceGaudi3;

class QPManagerGaudi3 : public QPManager
{
public:
    QPManagerGaudi3(HclDeviceGaudi3& device);
    virtual ~QPManagerGaudi3() = default;

    virtual void registerQPs(const QPManagerHints& hints, const QpsVector& qps) override = 0;
    virtual void closeQPs(const QPManagerHints& hints) override                          = 0;

    virtual uint32_t getQPn(const QPManagerHints& hints) const override = 0;
    virtual uint32_t getQPi(const QPManagerHints& hints) const override = 0;
    virtual uint32_t getQPi(const HCL_CollectiveOp collectiveOp, const bool isSend) override;
    virtual uint32_t getDestQPi(const unsigned qpi) const override;

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
                                      const bool              isReduction       = false,
                                      HCL_CollectiveOp        complexCollective = eHCLNoCollective,
                                      bool                    isRoot            = false) override;

    /* declared for the interface, but only implemented for scaleUp */
    virtual void setConfiguration(hcl::ScalStream& stream, HCL_Comm comm, bool isSend) override {};
};

class QPManagerGaudi3ScaleUp : public QPManagerGaudi3
{
public:
    QPManagerGaudi3ScaleUp(HclDeviceGaudi3& device);
    virtual ~QPManagerGaudi3ScaleUp() = default;

    virtual void registerQPs(const QPManagerHints& hints, const QpsVector& qps) override;
    virtual void closeQPs(const QPManagerHints& hints) override;
    virtual void setConfiguration(hcl::ScalStream& stream, HCL_Comm comm, bool isSend) override;

    virtual uint32_t getQPn(const QPManagerHints& hints) const override;
    virtual uint32_t getQPi(const QPManagerHints& hints) const override;

protected:
    virtual void
    setNicOffsets(hcl::ScalStream& stream, const HCL_Comm comm, const HCL_CollectiveOp collectiveOp, const bool isSend);

    virtual void setLastRankScaleup(hcl::ScalStream&       stream,
                                    const HCL_Comm         comm,
                                    const HCL_CollectiveOp collectiveOp,
                                    const bool             isSend);

    uint32_t getLastRankPortMask(HclDynamicCommunicator& dynamicComm,
                                 const HCL_CollectiveOp  collectiveOp,
                                 const bool              isSend) const;

private:
    void resizeDBForNewComms(HCL_Comm comm);
    void resizeOffsetDBs(HCL_Comm comm);

    std::array<uint16_t, MAX_NICS_GEN2ARCH>&
    getRemoteRankIndices(HCL_Comm comm, HCL_CollectiveOp collectiveOp, bool isSend);

    // m_qpInfoScaleUp[comm][qpi] -> qpn
    std::vector<std::array<QPn, MAX_QPS_PER_CONNECTION_G3>> m_qpInfoScaleUp;

    std::vector<std::array<uint16_t, MAX_NICS_GEN2ARCH>> m_remoteRankOffsets;
    std::vector<std::array<uint16_t, MAX_NICS_GEN2ARCH>> m_myRankOffsets;
};

class QPManagerGaudi3ScaleOut : public QPManagerGaudi3
{
public:
    QPManagerGaudi3ScaleOut(HclDeviceGaudi3& device);
    virtual ~QPManagerGaudi3ScaleOut() = default;

    virtual void registerQPs(const QPManagerHints& hints, const QpsVector& qps) override;
    virtual void allocateQPDBStorage(const HCL_Comm comm) override;
    virtual void closeQPs(const QPManagerHints& hints) override;

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
    void resizeDBForNewComms(HCL_Comm comm);
    void resizeDBPerComm(HCL_Comm comm);

    // m_qpInfoScaleOut[comm][remoteRank][qpSet][qpi] -> qpn
    std::vector<std::vector<std::array<std::array<QPn, MAX_QPS_PER_CONNECTION_G3>, MAX_QPS_SETS_PER_CONNECTION>>>
        m_qpInfoScaleOut;
};
