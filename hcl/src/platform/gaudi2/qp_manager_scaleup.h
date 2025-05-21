#pragma once

#include "qp_manager.h"

class HclDeviceGaudi2;

class QPManagerGaudi2ScaleUp : public QPManagerGaudi2
{
public:
    QPManagerGaudi2ScaleUp(HclDeviceGaudi2& device);
    virtual ~QPManagerGaudi2ScaleUp() = default;

    virtual void addQPsToQPManagerDB(const QPManagerHints& hints, const QpsVector& qps) override;
    virtual void ReleaseQPsResource(const QPManagerHints& hints) override;

    virtual uint32_t getQPn(const QPManagerHints& hints) const override;
    virtual uint32_t getQPi(const QPManagerHints& hints) const override;

private:
    // m_qpInfoScaleUp[nic][qpi] -> qpn
    std::array<std::array<QPn, MAX_QPS_PER_CONNECTION_G2>, MAX_NICS_GEN2ARCH> m_qpInfoScaleUp;
};
