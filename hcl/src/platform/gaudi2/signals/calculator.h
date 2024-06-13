#pragma once
#include "platform/gen2_arch_common/signals/calculator.h"
class SignalsCalculatorGaudi2 : public SignalsCalculator
{
public:
    SignalsCalculatorGaudi2()          = default;
    virtual ~SignalsCalculatorGaudi2() = default;
    virtual bool useRndvAckSignaling() override;
    virtual bool alwaysUseEdmaWorkDistribution() override;
};
