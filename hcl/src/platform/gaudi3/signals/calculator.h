#pragma once
#include "platform/gen2_arch_common/signals/calculator.h"

class SignalsCalculatorGaudi3 : public SignalsCalculator
{
public:
    SignalsCalculatorGaudi3()          = default;
    virtual ~SignalsCalculatorGaudi3() = default;
    virtual bool useRndvAckSignaling() override;
    virtual bool alwaysUseEdmaWorkDistribution() override;
};
