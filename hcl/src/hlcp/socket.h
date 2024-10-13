#pragma once

#include "asio.h"
#include <cstdint>

using socketfd_t = int;

constexpr socketfd_t INVALID_SOCKET = (socketfd_t)-1;

class socket_base_t;
class socket_op_notify_t  // socket operations
{
public:
    virtual void on_accept(socket_base_t& s, socketfd_t new_socket) _DEF_IMPL_;  // server only, new connected endpoint
    virtual void on_disconnect(socket_base_t& s) _DEF_IMPL_;
    virtual void on_error(socket_base_t& s) _DEF_IMPL_;
};

class socket_base_t : public socket_op_notify_t
{
public:
    socket_base_t() = default;
    socket_base_t(socketfd_t socket);
    socket_base_t(socket_op_notify_t& n) : op_notify_(&n) {}
    socket_base_t(socketfd_t s, socket_op_notify_t& n) : socket_base_t(s) { op_notify_ = &n; }

    virtual ~socket_base_t() { close(); }

    virtual bool send(void* data, size_t size) { return false; };
    virtual bool recv(void* data, size_t size) { return false; };

    bool close();

    bool close_socket();

    const sockaddr_t& local_addr  = local_;
    const sockaddr_t& remote_addr = remote_;
    const socketfd_t& fd          = socket_;

    socket_op_notify_t* op_notify_ = this;

    std::string str() const;

    bool set_non_blocking(bool non_blocking = true);

protected:
    virtual bool create(sa_family_t domain, int sock_type = SOCK_STREAM);
    virtual bool create(const sockaddr_t& addr);

    bool get_info();
    bool set_linger(bool set, uint32_t seconds);

    socketfd_t socket_ = INVALID_SOCKET;
    sockaddr_t local_;
    sockaddr_t remote_;
};

class async_socket_t
: public socket_base_t
, public asio_client_t
{
public:
    async_socket_t() = default;
    async_socket_t(socketfd_t s) : socket_base_t(s) {}
    async_socket_t(socket_op_notify_t& n) : socket_base_t(n) {}
    async_socket_t(socketfd_t s, socket_op_notify_t& n, asio_t* a) : socket_base_t(s, n), asio_client_t(a) {}

public:  // asio
    virtual int      io_fd() const override { return fd; };
    virtual uint32_t events() const override { return events_; };

    //    EPOLLONESHOT (since Linux 2.6.2)
    //           Requests one-shot notification for the associated file
    //           descriptor.  This means that after an event notified for
    //           the file descriptor by epoll_wait(2), the file descriptor
    //           is disabled in the interest list and no other events will
    //           be reported by the epoll interface.  The user must call
    //           epoll_ctl() with EPOLL_CTL_MOD to rearm the file
    //           descriptor with a new event mask.

protected:
    uint32_t events_ = (EPOLLONESHOT | EPOLLET | EPOLLRDHUP);
};

struct packet_t
{
    void*  buf  = nullptr;
    size_t size = 0;
    packet_t(void* b = nullptr, size_t s = 0) : buf(b), size(s) {}
};

class socket_io_notify_t  // read / write notify
{
public:
    virtual void on_send(const packet_t& p, socket_base_t& s) _DEF_IMPL_;  // send completed
    virtual void on_recv(const packet_t& p, socket_base_t& s) _DEF_IMPL_;  // recv completed
};

class socket_io_t
: public async_socket_t
, public socket_io_notify_t
{
private:
    struct  // send/recv descriptor
    {
        bool     active = false;
        size_t   offset = 0;
        packet_t packet;

        operator void*() { return (uint8_t*)packet.buf + offset; }
        operator const packet_t&() { return packet; }
        operator ssize_t() { return packet.size - offset; }
        auto& operator+=(size_t _x)
        {
            offset += _x;
            return *this;
        }

        auto& operator=(const packet_t& p)
        {
            VERIFY(!active, "operation in progress");

            active = true;
            offset = 0;
            packet = p;

            return *this;
        }

    } rx_, tx_;

public:
    socket_io_t() = default;
    socket_io_t(socketfd_t s) : async_socket_t(s) {}
    socket_io_t(socket_op_notify_t& n) : async_socket_t(n) {}
    socket_io_t(socketfd_t s, socket_op_notify_t& n, asio_t* a) : async_socket_t(s, n, a) {}

    socket_io_notify_t* io_notify_ = this;

public:  // asio
    virtual int io_event(uint32_t events) override;

public:  // socket_base
    virtual bool send(void* data, size_t size) override;
    virtual bool recv(void* data, size_t size) override;

private:
    void op_complete(bool send);
    void set_op(bool send, bool on);  // send:recv on:off

    int send();  // called when socket is ready to send
    int recv();  // called when socket is ready to recv (pending data)
};

class socket_t : public socket_io_t
{
public:
    socket_t() = default;
    socket_t(socketfd_t s) : socket_io_t(s) {};
    socket_t(socket_op_notify_t& n) : socket_io_t(n) {}
    socket_t(socketfd_t s, socket_op_notify_t& n, asio_t* a) : socket_io_t(s, n, a) {}

    bool connect(const sockaddr_t& peer, uint32_t /* timeout */ sec, const std::string& if_name = "");
};

//
// POSIX says that EAGAIN and EWOULDBLOCK may be identical, but also that they may
// be distinct.  Therefore, well-written portable code MUST check for both values
// in some circumstances.
//
// error: logical ‘or’ of equal expressions [-Werror=logical-op]
//
static inline bool would_block()
{
#if EAGAIN == EWOULDBLOCK
    return (errno == EAGAIN);
#else
    return ((errno == EAGAIN) || (errno == EWOULDBLOCK));
#endif
}

std::ostream& operator<<(std::ostream& out, const socket_base_t& s);
