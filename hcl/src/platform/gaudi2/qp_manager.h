#pragma once

#include "platform/gen2_arch_common/qp_manager.h"
#include "platform/gen2_arch_common/types.h"
#include "hcl_types.h"

#include <array>
#include <vector>

constexpr unsigned MAX_QPS_PER_CONNECTION_G2 = 4;

namespace G2
{
enum QP_e
{
    QPE_RS_RECV = 0,
    QPE_AG_RECV,
    QPE_RS_SEND,
    QPE_AG_SEND,
};
}

class HclDeviceGaudi2;

class QPManagerGaudi2 : public QPManager
{
public:
    QPManagerGaudi2(HclDeviceGaudi2& device);
    virtual ~QPManagerGaudi2() = default;

    virtual void registerQPs(const QPManagerHints& hints, const QpsVector& qps) override = 0;
    virtual void closeQPs(const QPManagerHints& hints) override                          = 0;

    virtual uint32_t getQPn(const QPManagerHints& hints) const override = 0;
    virtual uint32_t getQPi(const QPManagerHints& hints) const override = 0;
    virtual uint32_t getQPi(const HCL_CollectiveOp collectiveOp, const bool isSend) override;
    virtual uint32_t getDestQPi(const unsigned qpi) const override;
};

class QPManagerGaudi2ScaleUp : public QPManagerGaudi2
{
public:
    QPManagerGaudi2ScaleUp(HclDeviceGaudi2& device);
    virtual ~QPManagerGaudi2ScaleUp() = default;

    virtual void registerQPs(const QPManagerHints& hints, const QpsVector& qps) override;
    virtual void closeQPs(const QPManagerHints& hints) override;

    virtual uint32_t getQPn(const QPManagerHints& hints) const override;
    virtual uint32_t getQPi(const QPManagerHints& hints) const override;

private:
    void resizeDBForNewComms(HCL_Comm comm);

    // m_qpInfoScaleUp[comm][nic][qpi] -> qpn
    std::vector<std::array<std::array<QPn, MAX_QPS_PER_CONNECTION_G2>, MAX_NICS_GEN2ARCH>> m_qpInfoScaleUp;
};

class QPManagerGaudi2ScaleOut : public QPManagerGaudi2
{
public:
    QPManagerGaudi2ScaleOut(HclDeviceGaudi2& device);
    virtual ~QPManagerGaudi2ScaleOut() = default;

    virtual void registerQPs(const QPManagerHints& hints, const QpsVector& qps) override;
    virtual void closeQPs(const QPManagerHints& hints) override;
    virtual void allocateQPDBStorage(const HCL_Comm comm) override;

    virtual uint32_t getQPn(const QPManagerHints& hints) const override;
    virtual uint32_t getQPi(const QPManagerHints& hints) const override;

private:
    void resizeDBForNewComms(HCL_Comm comm);
    void resizeDBPerComm(HCL_Comm comm);

    // m_qpInfoScaleOut[comm][remoteRank][subNicIndex][qpSet][qpi] -> qpn
    std::vector<
        std::vector<std::array<std::array<std::array<QPn, MAX_QPS_PER_CONNECTION_G2>, MAX_QPS_SETS_PER_CONNECTION>,
                               COMPACT_RANK_INFO_NICS>>>
        m_qpInfoScaleOut;
};
