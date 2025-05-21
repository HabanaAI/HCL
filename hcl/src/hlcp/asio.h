#pragma once

#include <cstdint>
#include <deque>
#include <thread>
#include <atomic>

#include <sys/epoll.h>

#include "hlcp_inc.h"

//
// async IO
//
// we have a multithreaded IO server (asio_t) and IO clients (asio_client_t).
// IO client is represented with file descriptor ( virtual int io_fd() ) and
// registers events of interest with server, ( virtual uint32_t events() ).
// when event (or error) occurs, worker thread is awoken and calls client's callback
//   virtual int io_event(uint32_t events)
//
// f.e. IO client is a tcp socket and we want to receive data, so the event of interest is "IN", and we register it with
// the server. when data is arrived, thread is awoken and we shall successfully read() data from the socket.
//

class asio_t;

class asio_client_t
{
    friend asio_t;

    enum mode_bits_t
    {
        added = 0,
        armed = 1
    };  // bits in mode

protected:
    asio_t* asio  = nullptr;
    bits_t  mode_ = 0;

public:
    asio_client_t() = default;
    asio_client_t(asio_t* a) : asio(a) {}
    asio_client_t(const asio_client_t& o) = delete;
    // file descriptor of io client. can be file, pipe, socket....
    virtual int io_fd() const = 0;

    // events of interest for this client
    virtual uint32_t events() const = 0;

    // callback called by asio_t when event occurs
    // return values: -1: exit loop, 1: rearm event, 0: do nothing (continue loop)
    virtual int io_event(uint32_t events) = 0;

    virtual bool arm_monitor();

    operator int() const { return io_fd(); }
    operator void*() const { return (void*)this; }
};

constexpr int IO_EXIT  = -1;
constexpr int IO_REARM = 1;
constexpr int IO_NONE  = 0;

using counter_t = std::atomic<uint64_t>;

class asio_t : public asio_client_t
{
public:
    asio_t(const asio_t& o) = delete;

    asio_t() : running_(0) {}
    virtual ~asio_t() { close(); }

    bool start(uint32_t io_threads);
    bool add_workers(uint32_t io_threads);
    bool stop();

    using asio_client_t::arm_monitor;  // to avoid compiler "hides overloaded virtual function" error
    bool arm_monitor(asio_client_t& ioc);
    bool remove(asio_client_t& ioc);

private:
    int op_mode(asio_client_t& ioc);

private:  // asio_client_t for control pipe
    virtual int      io_event(uint32_t events) override;
    virtual int      io_fd() const override { return control_[0]; };
    virtual uint32_t events() const override { return EPOLLIN; };

private:
    bool      setup();
    bool      close();
    void      epoll_thread();
    counter_t running_;

    int epoll_fd_ = -1;

    // control pipe [read, write]
    int control_[2] = {-1, -1};
};
