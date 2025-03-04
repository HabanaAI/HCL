#pragma once
#include "protocol.h"
#include "socket.h"

class hlcp_t;

class hlcp_notify_t
{
public:
    // any requested command i.e. when you call connection.receive() it ends here
    virtual void on_message([[maybe_unused]] const hlcp_message_t& msg, [[maybe_unused]] hlcp_t& connection) _DEF_IMPL_;

    // command without payload  connection.receive_command(cmd)
    virtual void on_command([[maybe_unused]] hlcp_command_t& cmd, [[maybe_unused]] hlcp_t& connection) _DEF_IMPL_;

    // accepted new connection (start handshake etc...)
    virtual void on_connect([[maybe_unused]] hlcp_t& connection) _DEF_IMPL_;

    // protocol errors
    virtual void on_error([[maybe_unused]] hlcp_command_t*      cmd,
                          [[maybe_unused]] const hlcp_packet_t& packet,
                          [[maybe_unused]] hlcp_t&              connection,
                          [[maybe_unused]] const std::string&   reason = "") _DEF_IMPL_;
};

// hlcp protocol endpoint. will fire notify events on network packet parsing.

class hlcp_t
: public socket_io_notify_t
, public hlcp_notify_t
{
protected:
    enum hlcp_state_e
    {
        header,
        payload,
        ack
    };

    struct hlcp_operation_descriptor  // hlcp operation (tx/rx) descriptor
    {
        hlcp_state_e    state;  // what we are expecting/sending (header - payload - ack)
        hlcp_command_t* cmd;    // user supplied command descriptor (for command with payload must exist until send
                                // operation is completed)
        hlcp_packet_t packet;   // packet being sent/received

        operator void*() { return &packet; }
    };

    struct : public hlcp_operation_descriptor  // TX current transmit data
    {
        bool  completed = false;
        auto& operator=(const hlcp_command_t& _cmd)
        {
            completed = false;
            packet    = _cmd;

            if (_cmd.payload_size() > 0)
            {
                cmd = const_cast<hlcp_command_t*>(&_cmd);
            }
            else
            {
                cmd = nullptr;
            }

            return (*this);
        }
    } tx_;

    struct : public hlcp_operation_descriptor  // RX current receive data
    {
        auto& operator=(hlcp_command_t& _cmd)
        {
            cmd = &_cmd;
            return (*this);
        }
    } rx_;

protected:
    socket_io_t* transport_ = nullptr;

public:  // socket io notify
    virtual void on_recv(const packet_t& p, socket_base_t& s) override;
    virtual void on_send(const packet_t& p, socket_base_t& s) override;

public:
    operator socket_io_t&() { return *transport_; }
    socket_io_t* operator->() { return transport_; }

protected:
    void set_transport(socket_io_t& s);

    bool inspect_header();

    bool send_header();
    bool send_payload();
    bool recv_header();
    bool recv_payload();

    bool check_payload();

public:
    hlcp_notify_t* notify_ = this;

    hlcp_t() = default;
    hlcp_t(socket_io_t& s) { set_transport(s); }
    hlcp_t(socket_io_t& s, hlcp_notify_t& n) : notify_(&n) { set_transport(s); }

    virtual ~hlcp_t() = default;

    bool send_command(const hlcp_command_t& cmd);
    bool send_command(const hlcp_command_t& cmd, uint32_t timeout_sec);

    bool receive();  // any command

    bool receive_command(hlcp_command_t& cmd);  // recv specific command
    bool receive_payload(hlcp_command_t& cmd);

    bool send_ack();
    bool recv_ack();
};
