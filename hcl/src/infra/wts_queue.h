#pragma once
#include "futex.h"
#include <deque>

/**   Waitable Thread-Safe queue.
 *
 * This class provides a thread-safe queue that allows threads to wait for data
 * to be available and pop it in a synchronized manner. It uses a lock and an
 * event mechanism to ensure thread safety and efficient signaling.

 * @class wts_queue_t
 * @brief A thread-safe queue implementation with wait-and-signal functionality.
 *
 *
 */
template<typename T>
class wts_queue_t
{
private:
    lock_t        lock_;
    event_t       ready_;  // signaled when data is available
    std::deque<T> que_;
    bool          exit_ = false;

public:
    wts_queue_t(const wts_queue_t&)            = delete;
    wts_queue_t& operator=(const wts_queue_t&) = delete;
    wts_queue_t(wts_queue_t&&)                 = delete;
    wts_queue_t& operator=(wts_queue_t&&)      = delete;

    wts_queue_t()          = default;
    virtual ~wts_queue_t() = default;

    /**
     * @brief Pushes a value into the queue.
     *
     * This method adds a value to the back of the queue and signals any waiting
     * threads that data is available.
     *
     * @param val The value to push into the queue.
     */
    void push(const T& val)
    {
        locker_t locker(lock_);

        que_.push_back(val);
        ready_.signal();
    }

    /**
     * @brief Waits for data to be available and pops it from the queue.
     *
     * This method blocks the calling thread until data is available in the queue.
     * If the queue is empty or the release() method has been called, it returns false.
     * Otherwise, it retrieves the front value from the queue and removes it.
     *
     * @param val Reference to store the popped value.
     * @return true if a value was successfully popped, false if the queue is empty or released.
     */
    bool wait_and_pop(T& val)
    {
        ready_.wait();

        locker_t locker(lock_);

        if (exit_ || que_.empty())
        {
            return false;
        }

        val = que_.front();
        que_.pop_front();

        if (que_.empty())
        {
            ready_.reset();  // no more data. wait() will be blocked until push() is called
        }

        return true;
    }

    /**
     * @brief Releases the queue and signals waiting threads.
     *
     * This method sets the exit flag to true and signals any waiting threads,
     * causing them to unblock. Subsequent calls to wait_and_pop() will return false.
     */
    void release()
    {
        exit_ = true;
        ready_.signal();
    }
};

/**
 * This class extends the `wts_queue_t` class to provide a mechanism for dispatching
 * elements from the queue to a user-defined function in a separate worker thread.
 *
 * it takes a callable function (dispatcher_func_t) as a parameter,
 * which defines how each item in the queue should be processed.
 *
 */
template<class T>
class dispatcher_queue_t : public wts_queue_t<T>
{
private:
    std::thread worker_;

public:
    /**
     * @typedef dispatcher_func_t
     * @brief A type alias for the function used to process elements from the queue.
     */
    using dispatcher_func_t = std::function<void(const T&)>;

    /**
     * @brief Constructs a dispatcher queue with a user-defined processing function.
     *
     * @param func The function to be called for each element dequeued from the queue.
     *             This function is executed in the context of the worker thread.
     */
    dispatcher_queue_t(const dispatcher_func_t& func)
    {
        worker_ = std::thread([=]() {
            T val;
            while (wts_queue_t<T>::wait_and_pop(val))
            {
                func(val);
            }
        });
    }

    /**
     * @brief Destructor for the dispatcher queue.
     *
     * Ensures that the worker thread is properly joined and the queue is released
     * before the object is destroyed.
     */
    virtual ~dispatcher_queue_t()
    {
        wts_queue_t<T>::release();
        worker_.join();
    }
};