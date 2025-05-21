#pragma once

#include <cstdint>

/**
 * A class that provides a futex-based event mechanism. It allows thread(s) to wait for another thread to signal an
 * event. The waiting thread(s) will be blocked until the event is signaled.
 */
class futex_event_t
{
public:
    futex_event_t(const futex_event_t&)            = delete;
    futex_event_t& operator=(const futex_event_t&) = delete;
    futex_event_t(futex_event_t&&)                 = delete;
    futex_event_t& operator=(futex_event_t&&)      = delete;

    futex_event_t(bool signaled = false) : futex_(signaled ? SIGNALLED : NON_SIGNALLED) {}
    virtual ~futex_event_t() = default;

    // return: true - SIGNALLED, false - timeout occurred  (-1 == indefinitely)
    bool wait(int64_t msec = -1) const;  // if event is in non-signaled state, wait is blocking
    void signal();                       // signal the event, unblock all waiting threads
    void reset();                        // reset the event to non-signaled state
    bool signaled() const { return futex_ == SIGNALLED; }

private:
    enum event_state_t : uint32_t
    {
        NON_SIGNALLED = 0,
        SIGNALLED     = 1
    };

    volatile uint32_t futex_;  // when signaled, wait is NOT blocking
};

using event_t = futex_event_t;
