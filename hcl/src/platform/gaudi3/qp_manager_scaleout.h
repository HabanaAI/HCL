#pragma once

#include "qp_manager.h"

class HclDeviceGaudi3;

class QPManagerGaudi3ScaleOut : public QPManagerGaudi3
{
public:
    QPManagerGaudi3ScaleOut(HclDeviceGaudi3& device);
    virtual ~QPManagerGaudi3ScaleOut() = default;

    virtual void addQPsToQPManagerDB(const QPManagerHints& hints, const QpsVector& qps) override;
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

    void resizeDBPerComm(size_t commSize);

private:
    // m_qpInfoScaleOut[remoteRank][qpSet][qpi] -> qpn
    std::vector<std::array<std::array<QPn, MAX_QPS_PER_CONNECTION_G3>, MAX_QPS_SETS_PER_CONNECTION>> m_qpInfoScaleOut;
};
