#pragma once

#include "qp_manager.h"

class HclDeviceGaudi2;

class QPManagerGaudi2ScaleOut : public QPManagerGaudi2
{
public:
    QPManagerGaudi2ScaleOut(HclDeviceGaudi2& device);
    virtual ~QPManagerGaudi2ScaleOut() = default;

    virtual void addQPsToQPManagerDB(const QPManagerHints& hints, const QpsVector& qps) override;
    virtual void ReleaseQPsResource(const QPManagerHints& hints) override;

    virtual uint32_t getQPn(const QPManagerHints& hints) const override;
    virtual uint32_t getQPi(const QPManagerHints& hints) const override;

    void resizeDBPerComm(size_t commSize);

private:
    // m_qpInfoScaleOut[remoteRank][subNicIndex][qpSet][qpi] -> qpn
    std::vector<std::array<std::array<std::array<QPn, MAX_QPS_PER_CONNECTION_G2>, MAX_QPS_SETS_PER_CONNECTION>,
                           COMPACT_RANK_INFO_NICS>>
        m_qpInfoScaleOut;
};
