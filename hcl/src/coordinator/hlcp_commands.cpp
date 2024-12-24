#include "hlcp_commands.h"
#include <unordered_map>

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
    };

    return hlcp_cmd_names[id];
}