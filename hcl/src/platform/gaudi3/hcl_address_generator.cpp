
#include "platform/gaudi3/hcl_address_generator.h"
#include "platform/gen2_arch_common/collective_states.h"

uint64_t HclAddressGeneratorGaudi3::recalcAddressForDisragardRank(const HCL_CollectiveOp currentOp,
                                                                  const uint64_t         address,
                                                                  const uint64_t         offset)
{
    if (currentOp == eHCLScatter)
    {
        return address + offset;
    }

    return address;
}
