#include "futex.h"

#include <hcl_utils.h>    // for VERIFY
#include <climits>        // for INT_MAX
#include <linux/futex.h>  // for FUTEX_WAIT, FUTEX_WAKE
#include <cstring>        // for strerror
#include <syscall.h>      // for SYS_futex
#include <unistd.h>       // for syscall
#include <cerrno>         // for errno, EAGAIN

/**
 * According to man futex(2), the glibc wrapper of futex is not defined, only the system call. Here it is.
 */
static int
futex(const volatile uint32_t* uaddr, uint32_t futex_op, uint32_t val, const struct timespec* timeout = nullptr)
{
    return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr, 0);
}

// ======================================================================================================================

timespec* create_timeout_spec(timespec& timeout, int64_t timeout_ms)
{
    if (timeout_ms == -1) return nullptr;

    timeout.tv_sec  = timeout_ms / 1000;
    timeout.tv_nsec = (timeout_ms % 1000) * 1000000;

    return &timeout;
}

// true - SIGNALLED, false - timeout occurred  (-1 == indefinitely)
bool futex_event_t::wait(int64_t msec) const
{
    timespec  timeout;
    timespec* timeout_ptr = create_timeout_spec(timeout, msec);
    while (true)
    {
        // FUTEX_WAIT will immediately return if the value at the futex address is not equal to NON_SIGNALLED
        int result = futex(&futex_, FUTEX_WAIT, NON_SIGNALLED, timeout_ptr);
        if ((result == -1) && (errno == ETIMEDOUT))
        {
            return false;
        }
        VERIFY(0 == result || errno == EAGAIN, "futex(FUTEX_WAIT) failed with errno({})", strerror(errno));
        if (futex_ == SIGNALLED)
        {
            return true;
        }
    }
}

void futex_event_t::signal()
{
    futex_     = SIGNALLED;
    int result = futex(&futex_, FUTEX_WAKE, INT_MAX);
    VERIFY(-1 != result, "futex(FUTEX_WAKE) failed with errno({})", strerror(errno));
}

void futex_event_t::reset()
{
    futex_ = NON_SIGNALLED;
}
