#include "futex.h"

#include <hcl_utils.h>      // for VERIFY
#include <climits>          // for INT_MAX
#include <linux/futex.h>    // for FUTEX_WAIT, FUTEX_WAKE
#include <cstring>          // for strerror
#include <syscall.h>        // for SYS_futex
#include <unistd.h>         // for syscall
#include <cerrno>           // for errno, EAGAIN

/**
 * According to man futex(2), the glibc wrapper of futex is not defined, only the system call. Here it is.
 */
static int futex(int *uaddr, int futex_op, int val, const struct timespec *timeout, int *uaddr2, int val3)
{
    return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr, val3);
}

FutexLock::FutexLock() : m_data(0)
{
}

void FutexLock::lock()
{
    int current = __sync_val_compare_and_swap(&m_data, 0, 1);

    // If the lock was previously unlocked, there's nothing else for us to do. Otherwise, we'll probably have to wait.
    if (current != 0)
    {
        do
        {
            // If the mutex is locked, we signal that we're waiting by setting the atom to 2. A shortcut checks if it's
            // 2 already and avoids the atomic operation in this case.
            if (current == 2 || __sync_bool_compare_and_swap(&m_data, 1, 2) == true)
            {
                // Here we have to actually sleep, because the mutex is actually locked. Note that it's not necessary
                // to loop around this syscall; a spurious wakeup will do no harm since we only exit the do...while
                // loop when atom_ is indeed 0.
                int result = futex(&m_data, FUTEX_WAIT, 2, nullptr, nullptr, 0);
                VERIFY(0 == result || errno == EAGAIN, "futex(FUTEX_WAIT) failed with errno({})", strerror(errno));
            }
            // We're here when either:
            // (a) the mutex was in fact unlocked (by an intervening thread).
            // (b) we slept waiting for the atom and were awoken.
            //
            // So we try to lock the atom again. We set the state to 2 because we can't be certain there's no other
            // thread at this exact point. So we prefer to err on the safe side.
        } while ((current = __sync_val_compare_and_swap(&m_data, 0, 2)) != 0);
    }
}

/**
 * Release the lock using a CAS, and if successful notify other threads that we're done.
 */
void FutexLock::unlock()
{
    VERIFY(m_data > 0);
    // Attempt to release the futex by decreasing the data by 1. If the value was previously 1, then it's now 0 and
    // the futex is released. Otherwise, the value was 2 (thus a thread is waiting for FUTEX_WAKE) and we shall supply
    // it.
    if (__sync_fetch_and_sub(&m_data, 1) != 1)
    {
        // If we reached here, the value was 2 (a concurrent thread is blocked) and is now 1. We can safely force it
        // to 0 since (even if another thread is concurrently setting it to 2 again) concurrent threads will all be
        // woken up by FUTEX_WAKE.
        __sync_fetch_and_and(&m_data, 0); // set the value to 0 atomically.
        int result = futex((int32_t*) &m_data, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
        VERIFY(-1 != result, "futex(FUTEX_WAKE) failed with errno({})", strerror(errno));
    }
}
