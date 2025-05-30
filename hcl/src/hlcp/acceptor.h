#pragma once

#include "socket.h"

// server socket

class acceptor_t : public async_socket_t
{
public:
    virtual int io_event(uint32_t events) override;  // for accept() only

public:
    acceptor_t() : async_socket_t() { events_ = EPOLLIN | EPOLLONESHOT; }  // for accept
    acceptor_t(socket_op_notify_t& n) : async_socket_t(n) { events_ = EPOLLIN | EPOLLONESHOT; }

    bool listen(const sockaddr_t& addr);

    virtual std::string str() const override;

protected:
    bool accept();

private:
    virtual bool send(void*, size_t) override { return false; }
    virtual bool recv(void*, size_t) override { return false; }
};
