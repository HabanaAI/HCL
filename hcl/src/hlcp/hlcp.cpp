#include "hlcp.h"
#include <array>

void hlcp_t::set_transport(socket_io_t& s)
{
    transport_   = &s;
    s.io_notify_ = this;
}

bool hlcp_t::send_command(const hlcp_command_t& cmd)
{
    tx_ = cmd;
    return send_header();
}

bool hlcp_t::send_command(const hlcp_command_t& cmd, uint32_t timeout_sec)
{
    RET_ON_FALSE(send_command(cmd));

    wait_condition(tx_.completed, timeout_sec, cmd);

    return true;
}

bool hlcp_t::send_header()
{
    tx_.state = header;
    return transport_->send(tx_, sizeof(hlcp_packet_t));
}

bool hlcp_t::send_payload()
{
    tx_.state = payload;
    return transport_->send(tx_.cmd->payload(), tx_.cmd->payload_size());
}

void hlcp_t::on_send(const packet_t& p, socket_base_t& s)
{
    if (tx_.state == header)  // header send complete
    {
        if (tx_.packet.msg.payload_size > 0)
        {
            send_payload();
            return;
        }
    }

    tx_.completed = true;
}

bool hlcp_t::send_ack()
{
    HLCP_LOG("");

    tx_.state           = ack;
    tx_.completed       = false;
    tx_.packet.hdr.type = HLCP_PKT_ACK;

    return transport_->send(tx_, sizeof(hlcp_header_t));
}

bool hlcp_t::recv_ack()
{
    HLCP_LOG("");

    rx_.cmd   = nullptr;
    rx_.state = ack;

    return transport_->recv(rx_, sizeof(hlcp_header_t));
}

bool hlcp_t::recv_header()
{
    rx_.state = header;
    return transport_->recv(rx_, sizeof(hlcp_packet_t));
}

bool hlcp_t::recv_payload()
{
    HLCP_LOG("{}: {}", rx_.cmd->id(), transport_->str());

    VERIFY(rx_.cmd->payload(), "null payload");

    rx_.state = payload;

    return transport_->recv(rx_.cmd->payload(), rx_.cmd->payload_size());
}

bool hlcp_t::receive_command(hlcp_command_t& cmd)
{
    HLCP_LOG("{}: {}", cmd, transport_->str());

    rx_ = cmd;

    return recv_header();
}

bool hlcp_t::receive()
{
    HLCP_LOG("{}", transport_->str());

    rx_.cmd = nullptr;

    return recv_header();
}

bool hlcp_t::receive_payload(hlcp_command_t& cmd)
{
    HLCP_LOG("{}", cmd.id());

    rx_ = cmd;

    return recv_payload();
}

bool hlcp_t::inspect_header()
{
    const auto& hdr   = rx_.packet.hdr;
    const auto& magic = *(magic_t*)hdr.magic;
    //
    // check packet signature
    //
    return (magic == magic_t(HLCP_MAGIC)) && (hdr.version <= HLCP_VERSION) && (hdr.footer == HLCP_FOOTER) &&
           (rx_.state != ack || (hdr.type == HLCP_PKT_ACK));
}

bool hlcp_t::check_payload()
{
    if (rx_.packet.msg.payload_size > rx_.cmd->payload_size())
    {
        // network packet payload has more data then user expects
        notify_->on_error(rx_.cmd, rx_.packet, (*this), "payload size mismatch");
        return false;
    }

    return (rx_.cmd->payload_size() > 0);
}

void hlcp_t::on_recv(const packet_t& p, socket_base_t& s)
{
    HLCP_LOG("{}:{} state: {} {}", s, rx_.packet, rx_.state, rx_.cmd);

    if (rx_.state == payload)
    {
        // received full command with payload (header + message + payload)
        notify_->on_command(*rx_.cmd, (*this));
        return;
    }

    // state  == header, ack
    if (!inspect_header())
    {
        notify_->on_error(rx_.cmd, rx_.packet, (*this), "invalid header");
        return;
    }

    if (rx_.state == ack) return;

    // state == header

    if (!rx_.cmd)
    {
        // no user supplied cmd, so hlcp was requested to recv any
        notify_->on_message(rx_.packet.msg, (*this));
        return;
    }

    // user did asked for specific command
    if (rx_.cmd->id() != rx_.packet.msg.id)
    {
        // but different one has arrived
        notify_->on_error(rx_.cmd, rx_.packet, (*this), "unexpected message id");
        return;
    }

    // copy command's param to user's buffer
    *(rx_.cmd) = rx_.packet.msg;

    if (check_payload())
    {
        // we can receive payload
        recv_payload();
        return;
    }

    // no payload expected. received full command
    notify_->on_command(*rx_.cmd, (*this));
}
