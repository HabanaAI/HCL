#pragma once

#include "socket.h"

// server socket

class acceptor_t : public async_socket_t
{
public:
    virtual int io_event(uint32_t events) override;  // for accept() only

public:
    acceptor_t() : async_socket_t() { events_ |= EPOLLIN; }  // for accept
    acceptor_t(socket_op_notify_t& n) : async_socket_t(n) { events_ |= EPOLLIN; }

    bool listen(const sockaddr_t& addr);

protected:
    bool accept();

private:
    virtual bool send(void* data, size_t size) override { return false; }
    virtual bool recv(void* data, size_t size) override { return false; }
};
