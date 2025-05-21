#include "hlcp_commands.h"
#include <unordered_map>
#include "hlcp.h"

const char* cmd2str(cmdid_t id)
{
    static std::unordered_map<cmdid_t, const char*> hlcp_cmd_names = {
        {HLCP_SYNC, "HLCP_SYNC"},
        {HLCP_NON_PEERS, "HLCP_NON_PEERS"},
        {HLCP_RANK_DATA, "HLCP_RANK_DATA"},
        {HLCP_COMM_DATA, "HLCP_COMM_DATA"},
        {HLCP_QPS_CONF, "HLCP_QPS_CONF"},
        {HLCP_NIC_STATE, "HLCP_NIC_STATE"},
        {HLCP_LOG_MSG, "HLCP_LOG_MSG"},
        {HLCP_COUNTERS_DATA, "HLCP_COUNTERS_DATA"},
    };

    return hlcp_cmd_names[id];
}

hlcp_cmd_rank_data_t::hlcp_cmd_rank_data_t(const hlcp_message_t& msg, class hlcp_t& conn)
: _hlcp_cmd_rank_data_t(msg, conn)
{
    _hlcp_cmd_rank_data_t::param_.info.caddr = conn->remote_addr;
}
