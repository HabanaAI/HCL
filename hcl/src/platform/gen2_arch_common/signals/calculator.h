#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <map>

#include "platform/gen2_arch_common/signals/types.h"

class CommonState;

class SignalsCalculator
{
public:
    SignalsCalculator();
    virtual ~SignalsCalculator();

    void initialize(CommonState& commonState);

    static SignalsCalculator* create(bool isGaudi3);

    unsigned signalToCost(SignalEvent signal);

    virtual bool useRndvAckSignaling() = 0;

    virtual bool alwaysUseEdmaWorkDistribution() = 0;

protected:
    static std::map<uint32_t, std::string> m_signalNames;

    std::array<unsigned, (unsigned)SignalEvent::SIGNAL_EVENT_MAX> m_costs;
};