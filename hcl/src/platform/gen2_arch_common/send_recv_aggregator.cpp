#include "platform/gen2_arch_common/send_recv_aggregator.h"

#include <algorithm>  // for max
#include <cstdint>    // for uint32_t

#include "hcl_utils.h"        // for LOG_HCL_TRACE
#include "hcl_log_manager.h"  // for LOG_*

std::ostream& operator<<(std::ostream& os, const SendRecvEntry& entry)
{
    os << "{ address: 0x" << std::hex << entry.address << std::dec << ", count=" << entry.count
       << ", dataType=" << entry.dataType << ", isValid=" << entry.isValid << ", remoteRank=" << entry.remoteRank
       << "}";
    return os;
}

std::ostream& operator<<(std::ostream& os, const SendRecvVector& sendRecvVector)
{
    os << "[ size=" << sendRecvVector.size() << ", [ ";
    for (auto entry : sendRecvVector)
    {
        if (entry.isValid)
        {
            os << entry << ", ";
        }
        else
        {
            os << "{Invalid}, ";
        }
    }
    os << "] ]";
    return os;
}

std::ostream& operator<<(std::ostream& os, const SendRecvArray& sendRecvArray)
{
    os << "[ ";
    for (auto entry : sendRecvArray)
    {
        os << entry << ", ";
    }
    os << " ]";
    return os;
}

bool SendRecvAggregatorBase::willFlush()
{
    return m_aggEntryArrays.size() > 0;
}
