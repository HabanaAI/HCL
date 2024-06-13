#include "platform/gaudi3/signals/calculator.h"
#include "hcl_global_conf.h"  // for GFCG_*...
#include "hlthunk.h"          // for hlthunk_nic_user_get_ap...

bool SignalsCalculatorGaudi3::useRndvAckSignaling()
{
    return true;
}

bool SignalsCalculatorGaudi3::alwaysUseEdmaWorkDistribution()
{
    return true;
}
