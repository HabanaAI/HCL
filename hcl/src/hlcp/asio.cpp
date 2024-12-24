#include "asio.h"
#include <unistd.h>
#include <string>
#include <string.h>

std::string events_to_str(uint32_t events)
{
    std::string result;

    if (events & EPOLLIN)
    {
        result += "EPOLLIN ";
    }
    if (events & EPOLLPRI)
    {
        result += "EPOLLPRI ";
    }
    if (events & EPOLLOUT)
    {
        result += "EPOLLOUT ";
    }
    if (events & EPOLLRDNORM)
    {
        result += "EPOLLRDNORM ";
    }
    if (events & EPOLLRDBAND)
    {
        result += "EPOLLRDBAND ";
    }
    if (events & EPOLLWRNORM)
    {
        result += "EPOLLWRNORM ";
    }
    if (events & EPOLLWRBAND)
    {
        result += "EPOLLWRBAND ";
    }
    if (events & EPOLLMSG)
    {
        result += "EPOLLMSG ";
    }
    if (events & EPOLLERR)
    {
        result += "EPOLLERR ";
    }
    if (events & EPOLLHUP)
    {
        result += "EPOLLHUP ";
    }
    if (events & EPOLLRDHUP)
    {
        result += "EPOLLRDHUP ";
    }
    if (events & EPOLLEXCLUSIVE)
    {
        result += "EPOLLEXCLUSIVE ";
    }
    if (events & EPOLLWAKEUP)
    {
        result += "EPOLLWAKEUP ";
    }
    if (events & EPOLLONESHOT)
    {
        result += "EPOLLONESHOT ";
    }
    if (events & EPOLLET)
    {
        result += "EPOLLET ";
    }

    return result;
}

bool asio_t::setup()
{
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1)
    {
        return false;
    }

    HLCP_LOG("epoll_fd:{} {}", epoll_fd_, this);

    // create pipe to control thread loop (now for exit only)
    RET_ON_ERR(pipe(control_));

    // Add the read end of the pipe to the epoll set
    epoll_event event = {};

    event.events   = EPOLLIN;
    event.data.ptr = this;

    return epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, control_[0], &event) != -1;
}

bool asio_t::start(uint32_t io_threads)
{
    RET_ON_FALSE(setup());

    add_workers(io_threads);

    while (running_ < io_threads)
    {
        usleep(1000);
    }

    return true;
}

bool asio_t::add_workers(uint32_t io_threads)
{
    HLCP_LOG("{} workers", io_threads);

    FOR_I(io_threads)
    {
        std::thread(&asio_t::epoll_thread, this).detach();
    }

    return true;
}

bool asio_t::stop()
{
    HLCP_LOG("{}", this);
    constexpr uint32_t SIG_STOP = 0xC0DE0FF;
    // Send a stop signal through the pipe
    // it will wake all the threads and instruct them to stop
    return write(control_[1], &SIG_STOP, sizeof(SIG_STOP)) == sizeof(SIG_STOP);
}

int asio_t::io_event(uint32_t events)
{
    HLCP_LOG("asio. exit received");
    return IO_EXIT;
}

bool asio_t::close()
{
    HLCP_LOG("running threads: {}", running_.load());

    if (running_ > 0)
    {
        stop();
        while (running_ > 0)
        {
            __builtin_ia32_pause();
        }
    }

    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, control_[0], nullptr);

    ::close(control_[1]);  // Close the write end of the pipe
    ::close(epoll_fd_);
    ::close(control_[0]);  // Close the read end of the pipe

    control_[0] = control_[1] = epoll_fd_ = -1;

    HLCP_LOG("closed");

    return true;
}

bool asio_t::remove(asio_client_t& ioc)
{
    HLCP_LOG("epoll_fd:{}, fd:{}", epoll_fd_, ioc.io_fd());
    ioc.asio = nullptr;

    epoll_event event = {};

    return epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, ioc, &event) != -1;
}

bool asio_client_t::arm_monitor()
{
    VERIFY(asio, "asio not set");

    return asio->arm_monitor(*this);
}

int asio_t::op_mode(asio_client_t& ioc)
{
    int op = EPOLL_CTL_MOD;

    if (!ioc.mode_[added])
    {
        ioc.asio         = this;
        ioc.mode_[added] = true;

        op = EPOLL_CTL_ADD;
    }

    return op;
}

bool asio_t::arm_monitor(asio_client_t& ioc)
{
    if (ioc.mode_[armed]) return true;

    int op = op_mode(ioc);

    epoll_event event = {};

    event.events   = ioc.events();
    event.data.ptr = ioc;

    HLCP_LOG("[{}], op:{}. epoll_fd:{}, fd:{}  [{}]",
             event.data.ptr,
             op,
             epoll_fd_,
             ioc.io_fd(),
             events_to_str(event.events));

    ioc.mode_[armed] = true;
    return epoll_ctl(epoll_fd_, op, ioc, &event) != -1;
}

void asio_t::epoll_thread()
{
    HLCP_LOG("worker");

    epoll_event event = {};
    bool        stop  = false;

    running_++;

    while (!stop)
    {
        //
        // When successful, epoll_wait() returns the number of file descriptors ready for the requested I/O,
        // or zero if no file descriptor became ready during the requested timeout milliseconds (-1 == infinite).
        // When an error occurs, epoll_wait() returns -1 and errno is set appropriately.
        //
        // Errors
        // ...
        // EINTR
        // The call was interrupted by a signal handler before either any of the requested events occurred or the
        // timeout expired.
        //
        HLCP_LOG("epoll_wait({})", epoll_fd_);
        auto nfds = epoll_wait(epoll_fd_, &event, 1, -1);
        if (nfds == -1)
        {
            if (errno == EINTR) continue;

            HLCP_LOG("epoll_wait() failed: ({}) {}", errno, strerror(errno));
            break;
        }

        asio_client_t& ioc = *(asio_client_t*)event.data.ptr;

        ioc.mode_[armed] = false;

        HLCP_LOG("[{}] fd:{} events:{}", event.data.ptr, ioc.io_fd(), events_to_str(event.events));

        int rc = ioc.io_event(event.events);
        switch (rc)
        {
            case IO_REARM:
                arm_monitor(ioc);
                break;

            case IO_EXIT:  // exit loop
                stop = true;
                break;
        }
    }

    running_--;
}
