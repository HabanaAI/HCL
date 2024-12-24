#pragma once

#include "platform/gen2_arch_common/qp_manager.h"
#include "platform/gen2_arch_common/types.h"
#include "hcl_types.h"

#include <array>
#include <vector>

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

    virtual void addQPsToQPManagerDB(const QPManagerHints& hints, const QpsVector& qps) override = 0;
    virtual void ReleaseQPsResource(const QPManagerHints& hints) override                        = 0;

    virtual uint32_t getQPn(const QPManagerHints& hints) const override = 0;
    virtual uint32_t getQPi(const QPManagerHints& hints) const override = 0;
    virtual uint32_t getQPi(const HCL_CollectiveOp collectiveOp, const bool isSend) override;
    virtual uint32_t getDestQPi(const unsigned qpi) const override;
};