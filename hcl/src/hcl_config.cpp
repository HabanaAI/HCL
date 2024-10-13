#include "hcl_config.h"

#include "hcl_global_conf.h"  // for GCFG_*
#include "hcl_utils.h"        // for LOG_*

bool HclConfig::init(const HCL_Rank rank, const uint32_t ranksCount)
{
    VERIFY(m_commSize == 0 && m_jsonIndex == -1, "rank and count were already set");

    m_commSize  = ranksCount;
    m_jsonIndex = rank;

    if (isLoopbackMode())
    {
        // For loopback tests, determine the communicator size. As there is only
        // one process that actually is running, but in order to test collective
        // routines, more than one rank needs to exist in the communicator
        m_commSize = GCFG_LOOPBACK_COMMUNICATOR_SIZE.value();
    }

    return true;
}
