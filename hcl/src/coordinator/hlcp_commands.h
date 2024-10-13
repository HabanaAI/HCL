#pragma once
#include "protocol.h"
#include "hcl_types.h"
#include "hccl_internal_defs.h"

template<cmdid_t ID, class PARAM = uint32_t, class PAYLOAD = void*>
class _hlcp_command_t : public hlcp_command_t
{
public:
    _hlcp_command_t() = default;
    _hlcp_command_t(const PARAM& p, void* payload = nullptr, size_t size = 0)
    : param_(p), payload_(payload), payload_size_(size)
    {
    }
    _hlcp_command_t(const hlcp_message_t& msg)
    {
        VERIFY(msg.id == ID, "invalid cmd id: {} != {}", msg.id, ID);
        param_        = *(PARAM*)&msg.param;
        payload_size_ = msg.payload_size;
    }

    static_assert(sizeof(PARAM) <= HLCP_MAX_PARAM_SIZE);

    virtual cmdid_t id() const override { return ID; }
    virtual void*   param() const override { return (void*)&param_; }
    virtual size_t  param_size() const override { return sizeof(PARAM); }
    virtual void*   payload() const override { return payload_; }
    virtual size_t  payload_size() const override { return payload_size_; }

    PARAM   param_;
    PAYLOAD payload_      = nullptr;
    size_t  payload_size_ = 0;
    bool    completed_    = false;
};

struct __attribute__((packed)) hlcp_rank_data_param_t
{
    RankInfoHeader info      = {0};
    uint32_t       hlcp_port = -1;
    uint32_t       comm_size = 0;
};

constexpr cmdid_t HLCP_RANK_DATA = HLCP_BASE_CMD_ID + 10;  // client -> server
using hlcp_cmd_rank_data_t       = _hlcp_command_t<HLCP_RANK_DATA, hlcp_rank_data_param_t>;

// comm group configuration
constexpr cmdid_t HLCP_COMM_DATA = HLCP_BASE_CMD_ID + 20;  // server -> client
using hlcp_cmd_comm_data_t       = _hlcp_command_t<HLCP_COMM_DATA, HCL_Rank>;

// qps configuration
constexpr cmdid_t HLCP_QPS_CONF = HLCP_BASE_CMD_ID + 30;  // client -> server -> client
using hlcp_cmd_qps_conf_t       = _hlcp_command_t<HLCP_QPS_CONF>;

// non peers qps conf
constexpr cmdid_t HLCP_NON_PEERS = HLCP_BASE_CMD_ID + 40;  // client -> client
using hlcp_cmd_non_peers_t       = _hlcp_command_t<HLCP_NON_PEERS, HCL_Rank>;

// collective log
constexpr cmdid_t HLCP_LOG_MSG = HLCP_BASE_CMD_ID + 50;  // client -> server
using hlcp_cmd_log_msg_t       = _hlcp_command_t<HLCP_LOG_MSG, CollectiveLogMessage>;

// sync (rendezvous)
constexpr cmdid_t HLCP_SYNC = HLCP_BASE_CMD_ID + 60;                 // client -> server; client -> client
using hlcp_cmd_sync_t       = _hlcp_command_t<HLCP_SYNC, HCL_Rank>;  //
