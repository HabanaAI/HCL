#pragma once
#include "hcl_log_manager.h"
#include <cstdint>
#include <cstddef>
#include <array>

// HL coordinator protocol HLCP

#pragma pack(push)
#pragma pack(1)

constexpr uint32_t HLCP_VERSION = 0xC0010001;
constexpr uint32_t HLCP_FOOTER  = 0xC0DE1B0B;

constexpr uint32_t HLCP_PKT_ACK  = 0xACED0001;
constexpr uint32_t HLCP_PKT_DATA = 0xACED0002;

using magic_t = std::array<char, 4>;
#define HLCP_MAGIC {'H', 'L', 'C', 'P'}

struct hlcp_header_t
{
    char     magic[4] = HLCP_MAGIC;
    uint32_t version  = HLCP_VERSION;
    uint32_t type     = HLCP_PKT_DATA;
    uint32_t footer   = HLCP_FOOTER;
};

using cmdid_t = uint32_t;

constexpr uint32_t HLCP_MAX_PARAM_SIZE = 512;

struct hlcp_message_t
{
    cmdid_t  id                         = 0;
    uint8_t  param[HLCP_MAX_PARAM_SIZE] = {};
    uint32_t payload_size               = 0;
};

class hlcp_command_t;
struct hlcp_packet_t
{
    hlcp_header_t  hdr;
    hlcp_message_t msg;

    // build packet from user supplied command
    hlcp_packet_t& operator=(const hlcp_command_t& cmd);
};

#pragma pack(pop)

// command prototype
class hlcp_command_t
{
public:
    virtual cmdid_t id() const { return 0; };

    virtual const char* name() const { return "empty"; };

    virtual void*  param() const { return nullptr; }
    virtual size_t param_size() const { return 0; }
    virtual void*  payload() const { return nullptr; }
    virtual size_t payload_size() const { return 0; }
    virtual ~hlcp_command_t() = default;

    hlcp_command_t& operator=(const hlcp_message_t& msg);
};

constexpr cmdid_t HLCP_BASE_CMD_ID = 100;

#include <ostream>
std::ostream& operator<<(std::ostream& out, const hlcp_header_t& hdr);
HLLOG_DEFINE_OSTREAM_FORMATTER(hlcp_header_t);
std::ostream& operator<<(std::ostream& out, const hlcp_message_t& msg);
HLLOG_DEFINE_OSTREAM_FORMATTER(hlcp_message_t);
std::ostream& operator<<(std::ostream& out, const hlcp_packet_t& p);
HLLOG_DEFINE_OSTREAM_FORMATTER(hlcp_packet_t);
std::ostream& operator<<(std::ostream& out, const hlcp_command_t& c);
HLLOG_DEFINE_OSTREAM_FORMATTER(hlcp_command_t);
