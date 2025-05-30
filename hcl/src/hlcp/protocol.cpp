#include "protocol.h"
#include "hlcp_inc.h"
#include "hcl_utils.h"  // for VERIFY
#include <cstring>

// build network packet from user supplied buffer  i.e. send
hlcp_packet_t& hlcp_packet_t::operator=(const hlcp_command_t& cmd)
{
    hdr.type = HLCP_PKT_DATA;
    msg.id   = cmd.id();

    std::memcpy(msg.param, cmd.param(), cmd.param_size());
    msg.payload_size = cmd.payload_size();

    return (*this);
}

// from network packet to user supplied buffer i.e. recv
hlcp_command_t& hlcp_command_t::operator=(const hlcp_message_t& msg)
{
    VERIFY(msg.id == id(), "invalid msg.id: {} != {}", msg.id, id());

    std::memcpy(param(), msg.param, param_size());
    return (*this);
}

std::ostream& operator<<(std::ostream& out, const hlcp_header_t& hdr)
{
    const magic_t& magic = *(magic_t*)hdr.magic;

    out << "hlcp_header_t[" << magic[0] << magic[1] << magic[2] << magic[3];

    out << std::hex << std::uppercase;
    out << " " << hdr.version << " " << hdr.type << " " << hdr.footer << "]";
    out << std::dec << std::nouppercase;

    return out;
}

std::ostream& operator<<(std::ostream& out, const hlcp_message_t& msg)
{
    return out << "hlcp_msg[" << msg.id << ", " << msg.payload_size << "]";
}

std::ostream& operator<<(std::ostream& out, const hlcp_packet_t& p)
{
    return out << "hlcp_packet(" << p.hdr << " " << p.msg << ")";
}

std::ostream& operator<<(std::ostream& out, const hlcp_command_t& c)
{
    out << "hlcp_cmd[" << c.name() << "(" << c.id() << "), " << c.param_size();
    if (c.payload())
    {
        out << ", payload: " << c.payload_size() << " @ " << c.payload();
    }
    out << "]";
    return out;
}
