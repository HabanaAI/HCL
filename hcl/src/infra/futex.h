#pragma once

#include <cstdint>

/**
 * A std::mutex-like class which allows us to use std::scoped_lock for this. Example:
 *
 * class MyClass {
 *     void run() {
 *         {
 *             std::scoped_lock<FutexLock> lock(m_myMutex);
 *             throw std::exception(); // scoped_lock ensures that FutexLock::unlock() is called.
 *         }
 *     }
 *     FutexLock m_myMutex;
 * };
 */
class FutexLock
{
public:
    FutexLock();
    FutexLock(const FutexLock& other) = delete;
    FutexLock(const FutexLock&& other) = delete;
    FutexLock& operator=(const FutexLock& other) = delete;
    FutexLock& operator=(const FutexLock&& other) = delete;

    /**
     * Attempt to acquire the lock pointed to by ptr by changing its value from 0 (not acquired) to 1 (acquired).
     * If the (atomic) Compare-And-Swap succeeds then we're done. Otherwise, use futex to wait until another thread
     * releases the futex. If futex() returns with EAGAIN (was concurrently swapped, see futex(2)) we retry, otherwise an
     * error condition occured.
     *
     * This implementation has the added benefit of having the acquisition done mostly in the user-space, only invoking
     * a syscall (kernel-space) if the CAS failed, in which case we wait for a release by a different thread in a
     * non-spin-lock way.
     */
    void lock();

    /**
     * Release the lock using a CAS, and if successful notify other threads that we're done.
     */
    void unlock();

private:
    int32_t m_data;
};
