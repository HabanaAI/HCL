#include "acceptor.h"

#define SERVER_SOCKET_MAX_CONNECTIONS 1000  // backlog

int acceptor_t::io_event(uint32_t io_events)
{
    HLCP_LOG("socket({}) events {}", socket_, io_events);

    if (io_events & EPOLLERR)
    {
        op_notify_->on_error(*this);
        return IO_NONE;
    }

    if (io_events & EPOLLIN)  // can accept
    {
        if (accept())
        {
            return IO_REARM;
        }
        else
        {
            return IO_NONE;
        }
    }

    HLCP_LOG("UNHANDLED !!! RE-ARM {} events: {}", socket_, io_events);

    return IO_REARM;
}

bool acceptor_t::accept()
{
    sockaddr_t peer_addr;
    socklen_t  addr_len = (socklen_t)peer_addr;

    while (true)
    {
        int peer = ::accept(socket_, peer_addr, &addr_len);
        if ((peer == -1) && would_block())
        {
            return true;
        }
        else if (peer != -1)
        {
            arm_monitor();
            op_notify_->on_accept(*this, peer);
        }
        else  // error ?
        {
            op_notify_->on_error(*this);
            return false;
        }
    }
}

bool acceptor_t::listen(const sockaddr_t& address)
{
    RET_ON_FALSE(create(address));

    local_ = address;

    int opt_val = 1;
    RET_ON_ERR(setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val)));

    RET_ON_ERR(bind(socket_, address, address));

    socklen_t addr_len = (socklen_t)local_;
    RET_ON_ERR(getsockname(socket_, local_, &addr_len));

    if ((address.port() != 0) && (address.port() != local_.port()))
    {
        HLCP_ERR("{} bound to {} instead of {}", socket_, local_.str(), address.str());
        return false;
    }

    RET_ON_ERR(::listen(socket_, SERVER_SOCKET_MAX_CONNECTIONS));

    RET_ON_FALSE(set_non_blocking());

    HLCP_LOG("socket({}): {}", socket_, local_.str());

    return true;
}
