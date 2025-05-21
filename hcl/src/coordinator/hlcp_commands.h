#pragma once
#include "protocol.h"
#include "hcl_types.h"
#include "hccl_internal_defs.h"
#include "qp_migration.h"

const char* cmd2str(cmdid_t id);

struct payload_t
{
    void*  ptr  = nullptr;
    size_t size = 0;
    payload_t(const void* p = nullptr, size_t s = 0) : ptr((void*)p), size(s) {}
};

/**
 * @class hlcp_payload_t
 * @brief A class that manages a payload buffer with automatic memory management.
 *
    hlcp_payload_t payload(0x1234, 4) ; // 4 bytes buffer starting from 0x1234
    or payload = buffer_t{0x1234, 4};

    hlcp_payload_t payload(400); // 400 bytes buffer allocated and then freed in destructor
    or payload = 400;

    void* ptr = payload;   // get pointer to the buffer
    size_t size = payload; // get size of the buffer

 * @note The class ensures that memory is properly released when the object is destroyed.
 */
class hlcp_payload_t
{
public:
    hlcp_payload_t() = default;
    hlcp_payload_t(const void* ptr, size_t size) : buffer_(ptr, size), owner_(false) {}
    hlcp_payload_t(size_t size) { alloc(size); }

    virtual operator void*() const { return buffer_.ptr; }
    virtual operator const void*() const { return buffer_.ptr; }
    virtual operator size_t() const { return buffer_.size; }

    auto& operator=(const payload_t& buf)
    {
        clear();
        buffer_ = buf;
        return *this;
    }

    auto& operator=(size_t s)
    {
        clear();
        alloc(s);
        return *this;
    }

    virtual ~hlcp_payload_t() { clear(); }

private:
    payload_t buffer_;
    bool      owner_ = false;  //

    void alloc(size_t size)
    {
        if (size > 0)
        {
            buffer_.ptr  = new uint8_t[size];
            buffer_.size = size;
            owner_       = true;
        }
    }

    void clear()
    {
        if (owner_)
        {
            delete[] (uint8_t*)buffer_.ptr;
            owner_ = false;
        }

        buffer_ = {};
    }
};

template<cmdid_t ID, class PARAM = uint32_t, class PAYLOAD = hlcp_payload_t>
class _hlcp_command_t : public hlcp_command_t
{
public:
    _hlcp_command_t() = default;

    explicit _hlcp_command_t(const PARAM& p, const void* payload = nullptr, size_t size = 0)
    : param_(p), payload_(payload, size)
    {
    }

    explicit _hlcp_command_t(const hlcp_message_t& msg, class hlcp_t&, bool alloc_payload = false)
    : _hlcp_command_t(msg, alloc_payload)
    {
    }

    explicit _hlcp_command_t(const hlcp_message_t& msg, bool alloc_payload = false)
    {
        VERIFY(msg.id == ID, "invalid cmd id: {} != {}", msg.id, ID);

        param_ = *(PARAM*)&msg.param;

        if (alloc_payload)
        {
            payload_ = msg.payload_size;
        }
    }

    auto& operator=(const hlcp_message_t& msg)
    {
        hlcp_command_t::operator=(msg);
        return *this;
    }

    static_assert(sizeof(PARAM) <= HLCP_MAX_PARAM_SIZE);

    virtual cmdid_t     id() const override { return ID; }
    virtual const char* name() const override { return cmd2str(ID); }
    virtual void*       param() const override { return (void*)&param_; }
    virtual size_t      param_size() const override { return sizeof(PARAM); }
    virtual void*       payload() const override { return static_cast<void*>(payload_); }
    virtual size_t      payload_size() const override { return static_cast<size_t>(payload_); }

    PARAM   param_;
    PAYLOAD payload_;

    bool completed_ = false;
};

// commands and params definitions
#pragma pack(push)
#pragma pack(1)

struct hlcp_rank_data_param_t
{
    RankInfoHeader info      = {0};
    uint32_t       hlcp_port = -1;
    uint32_t       comm_size = 0;
    HCL_Comm       comm      = HCL_INVALID_COMM;
};
constexpr cmdid_t HLCP_RANK_DATA = HLCP_BASE_CMD_ID + 10;  // client -> server
using _hlcp_cmd_rank_data_t      = _hlcp_command_t<HLCP_RANK_DATA, hlcp_rank_data_param_t>;
class hlcp_cmd_rank_data_t : public _hlcp_cmd_rank_data_t
{
public:
    hlcp_cmd_rank_data_t(const hlcp_message_t& msg, class hlcp_t& conn);
    hlcp_cmd_rank_data_t(const hlcp_rank_data_param_t& p) : _hlcp_cmd_rank_data_t(p) {}
};

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
// if sync is done at the end of FO/B, migration flag is set to true
struct hlcp_sync_param_t
{
    HCL_Rank rank;
    bool     migration;
    hlcp_sync_param_t(HCL_Rank r = HCL_INVALID_RANK, bool m = false) : rank(r), migration(m) {}
};
constexpr cmdid_t HLCP_SYNC = HLCP_BASE_CMD_ID + 60;  // client -> server; client -> client
using hlcp_cmd_sync_t       = _hlcp_command_t<HLCP_SYNC, hlcp_sync_param_t>;

using hlcp_nic_state_param_t     = NicState;
constexpr cmdid_t HLCP_NIC_STATE = HLCP_BASE_CMD_ID + 70;  // client -> server -> client
using hlcp_cmd_nic_state_t       = _hlcp_command_t<HLCP_NIC_STATE, hlcp_nic_state_param_t>;

struct hlcp_counters_param_t
{
    bool all_reached = false;
    hlcp_counters_param_t(bool _all_reached = false) : all_reached(_all_reached) {}
};

constexpr cmdid_t HLCP_COUNTERS_DATA = HLCP_BASE_CMD_ID + 80;  // client -> server; client -> client
using hlcp_cmd_counters_t            = _hlcp_command_t<HLCP_COUNTERS_DATA, hlcp_counters_param_t>;

//
// To add a new command:
//
// 1) Add the command ID, for example, XXX:
//
//       constexpr cmdid_t HLCP_XXX = HLCP_BASE_CMD_ID + 80;
//
// 1.1) update cmd2str() in hlcp_commands.cpp
//
//
//
// 2) If the command has a parameter (less than 512 bytes), define a struct for it and use it in _hlcp_command_t:
//
//       struct hlcp_xxx_param_t
//       {
//          ...
//       };
//
//       using hlcp_cmd_xxx_t = _hlcp_command_t<HLCP_XXX, hlcp_xxx_param_t>;
//
//
// 3) Define a handler for the command in hlcp_server_t and/or hlcp_client_t:
//
//       void on_hlcp_xxx(hlcp_cmd_xxx_t& cmd) {}
//
//
// 4) Add a call to the handler in the on_command() method in hlcp_server_t and/or hlcp_client_t:
//
//       HLCP_CMD_HANDLER(HLCP_XXX, hlcp_cmd_xxx_t, on_hlcp_xxx);
//
//
// 5.1) If the command is WITHOUT PAYLOAD, add the following to on_message():
//
//       HLCP_MSG_HANDLER(HLCP_XXX, hlcp_cmd_xxx_t);
//
//
// 5.2) If the command is WITH PAYLOAD and it's size is unknown at compile time add the following to on_message():
//
//      HLCP_MSG_PAYLOAD_HANDLER(HLCP_XXX, hlcp_cmd_xxx_t);
//
//      Remember to delete the command in the handler:
//      void on_hlcp_xxx(hlcp_cmd_xxx_t& cmd)
//      {
//         ...
//         delete &cmd;
//      }
//
// 5.3) If the command is WITH PAYLOAD, and needs caching from all ranks before sending back,
//      create a hlcp_server_t function to copy cached data from server to the payload, e.g.
//      send_xxx_data(uint32_t start_index, uint32_t count, sp_hlcp_cmd_t sp_cmd);
//
// 5.4) Add call to handle new message id in hlcp_client_t::on_message() case switch.
//

#pragma pack(pop)

#define HLCP_CMD_HANDLER(CMD_ID, CMD_TYPE, HANDLER)                                                                    \
    case CMD_ID:                                                                                                       \
        HANDLER(static_cast<CMD_TYPE&>(cmd));                                                                          \
        break

#define HLCP_MSG_HANDLER(MSG_ID, CMD_TYPE)                                                                             \
    case MSG_ID:                                                                                                       \
    {                                                                                                                  \
        CMD_TYPE command(msg, connection);                                                                             \
        on_command(command, connection);                                                                               \
        break;                                                                                                         \
    }

#define HLCP_MSG_PAYLOAD_HANDLER(MSG_ID, CMD_TYPE)                                                                     \
    case MSG_ID:                                                                                                       \
    {                                                                                                                  \
        auto& command = *new CMD_TYPE(msg, connection, true);                                                          \
        connection.receive_payload(command);                                                                           \
        break;                                                                                                         \
    }

#define HLCP_CMD_MSG_PAYLOAD_HANDLER(MSG_ID, CMD)                                                                      \
    case MSG_ID:                                                                                                       \
    {                                                                                                                  \
        CMD = msg;                                                                                                     \
        connection.receive_payload(CMD);                                                                               \
        break;                                                                                                         \
    }
