#pragma once
#include "acceptor.h"
#include "hlcp.h"

class coordinator_t
: public socket_op_notify_t
, public hlcp_notify_t
{
protected:
    class xsocket_t : public socket_io_t
    {
    private:
        hlcp_t* owner_ = nullptr;

    public:
        bool marked = false;  // marked for disconnect

        xsocket_t(socketfd_t s, socket_op_notify_t& n, asio_t* a) : socket_io_t(s, n, a) { set_non_blocking(); }
        auto& operator=(hlcp_t& p)
        {
            owner_ = &p;
            return (*this);
        }
        hlcp_t* operator->() const { return owner_; }
        operator hlcp_t&() { return *owner_; }
    };

protected:
    HCL_Comm comm_id_ = HCL_INVALID_COMM;

protected:
    asio_t     asio_;
    acceptor_t srv_;

protected:  // socket_op_notify_t
    virtual void on_error(socket_base_t& s) override;
    virtual void on_accept(socket_base_t& s, int new_socket_fd) override;  // srv new connection
    virtual void on_disconnect(socket_base_t& s) override;

protected:
    virtual hlcp_t& create_connection(xsocket_t& s) { return *(new hlcp_t(s, *this)); }
    virtual void    destroy_connection(hlcp_t& c) { delete &c; }
    virtual void    close_connection(hlcp_t& c);  // gracefully
    virtual void    drop_connection(hlcp_t& c);

public:  // hlcp_notify_t
    virtual void on_connect(hlcp_t& connection) override { connection.receive(); }
    virtual void on_error(hlcp_command_t*      cmd,
                          const hlcp_packet_t& packet,
                          hlcp_t&              connection,
                          const std::string&   reason = "") override;

public:
    coordinator_t() : srv_(*this) {};
    virtual ~coordinator_t() { stop(); };

    const acceptor_t* operator->() { return &srv_; }

    bool start(uint32_t io_threads, const sockaddr_t& addr = sockaddr_t());

    bool stop();
};

#define HCL_COORD_LOG(FMT, ...) COORD_LOG("[comm:{:2}] " FMT, comm_id_, ##__VA_ARGS__)
#define HCL_COORD_DBG(FMT, ...) COORD_DBG("[comm:{:2}] " FMT, comm_id_, ##__VA_ARGS__)
#define HCL_COORD_INF(FMT, ...) COORD_INF("[comm:{:2}] " FMT, comm_id_, ##__VA_ARGS__)
#define HCL_COORD_WRN(FMT, ...) COORD_WRN("[comm:{:2}] " FMT, comm_id_, ##__VA_ARGS__)
#define HCL_COORD_ERR(FMT, ...) COORD_ERR("[comm:{:2}] " FMT, comm_id_, ##__VA_ARGS__)
#define HCL_COORD_CRT(FMT, ...) COORD_CRT("[comm:{:2}] " FMT, comm_id_, ##__VA_ARGS__)
