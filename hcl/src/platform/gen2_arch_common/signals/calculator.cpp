#include "platform/gen2_arch_common/signals/calculator.h"
#include "platform/gaudi2/signals/calculator.h"
#include "platform/gaudi3/signals/calculator.h"
#include "platform/gen2_arch_common/collective_states.h"
#include "hcl_utils.h"
#include "hcl_global_conf.h"  // for GFCG_*...

SignalsCalculator::SignalsCalculator() {};
SignalsCalculator::~SignalsCalculator() {};

void SignalsCalculator::initialize(CommonState& commonState)
{
    unsigned signalsSingleOp           = commonState.countSignalsSingleOp();
    unsigned workDistributionGroupSize = commonState.m_workDistributionGroupSize;
    unsigned minimumEdmaGroupSize      = (unsigned)alwaysUseEdmaWorkDistribution() * workDistributionGroupSize +
                                    (unsigned)(!alwaysUseEdmaWorkDistribution());
    unsigned numScaleOutPorts = commonState.m_numScaleOutPorts;

    m_costs[(unsigned)SignalEvent::FORCE_ORDER] = 1;

    m_costs[(unsigned)SignalEvent::EDMA_CAST_DOWN]              = minimumEdmaGroupSize;
    m_costs[(unsigned)SignalEvent::EDMA_CAST_DOWN_FOR_SCALEOUT] = minimumEdmaGroupSize;
    m_costs[(unsigned)SignalEvent::EDMA_MEMCOPY]                = workDistributionGroupSize;
    m_costs[(unsigned)SignalEvent::EDMA_MEMCOPY_FOR_SCALEOUT]   = workDistributionGroupSize;
    m_costs[(unsigned)SignalEvent::EDMA_MEMSET] =
        GCFG_HCL_USE_EDMA_COMMAND_V3.value() ? workDistributionGroupSize : minimumEdmaGroupSize;
    m_costs[(unsigned)SignalEvent::EDMA_CAST_UP]        = workDistributionGroupSize;
    m_costs[(unsigned)SignalEvent::EDMA_BATCH]          = workDistributionGroupSize;
    m_costs[(unsigned)SignalEvent::EDMA_BATCH_SCALEOUT] = workDistributionGroupSize;
    m_costs[(unsigned)SignalEvent::EDMA_MEMCOPY_GDR]    = workDistributionGroupSize;
    m_costs[(unsigned)SignalEvent::EDMA_MEMCOPY_RR]     = workDistributionGroupSize;
    m_costs[(unsigned)SignalEvent::EDMA_MEMCOPY_RR_LAST_BOX] =
        commonState.m_16BitReduction ? minimumEdmaGroupSize
                                     : workDistributionGroupSize;  // work distribution for in order

    m_costs[(unsigned)SignalEvent::SCALEUP_SEND]       = signalsSingleOp;
    m_costs[(unsigned)SignalEvent::SCALEUP_RECV]       = signalsSingleOp * (useRndvAckSignaling() ? 2 : 1);
    m_costs[(unsigned)SignalEvent::SCALEOUT_SEND]      = numScaleOutPorts;
    m_costs[(unsigned)SignalEvent::SCALEOUT_RECV]      = numScaleOutPorts * (useRndvAckSignaling() ? 2 : 1);
    m_costs[(unsigned)SignalEvent::HNIC_SCALEOUT_SEND] = 1;
    m_costs[(unsigned)SignalEvent::HNIC_SCALEOUT_RECV] = 1;
    m_costs[(unsigned)SignalEvent::HNIC_PDMA]          = 1;
    m_costs[(unsigned)SignalEvent::RR_SIGNAL_TO_LONGTERM] =
        GCFG_HCL_USE_EDMA_COMMAND_V3.value() ? workDistributionGroupSize : 1;
    m_costs[(unsigned)SignalEvent::RR_SIGNAL_TO_CG]       = 1;
}

unsigned SignalsCalculator::signalToCost(SignalEvent signal)
{
    return m_costs[(unsigned)signal];
}

SignalsCalculator* SignalsCalculatorFactory::create(bool isGaudi3)
{
    static SignalsCalculator* Calculator = nullptr;

    if (!Calculator)
    {
        if (isGaudi3)
        {
            Calculator = new SignalsCalculatorGaudi3();
        }
        else
        {
            Calculator = new SignalsCalculatorGaudi2();
        }
    }

    return Calculator;
}