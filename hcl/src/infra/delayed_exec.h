#include <functional>  // for function
#include "futex.h"     // for event

/**
 * @class delayed_exec_t
 * @brief A utility class for executing a function after a specified delay, with support for cancellation.
 *
 * This class provides a mechanism to schedule a function to be executed after a given delay in milliseconds.
 * The execution can be canceled if needed before the delay expires. It uses threading and signaling mechanisms
 * to manage the delayed execution and cancellation.
 */
class delayed_exec_t
{
public:
    delayed_exec_t();
    virtual ~delayed_exec_t();

    /**
     * @brief Executes a given function after a specified delay.
     *
     * This function schedules the provided callable object to be executed
     * after a delay specified in milliseconds. It is useful for deferring
     * execution of tasks or implementing timed operations.
     *
     * @param func The function to be executed after the delay. It must be a callable
     *             object with no arguments and no return value.
     *
     * @param delay_ms The delay in milliseconds before the function is executed.
     */
    void delay_execution(const std::function<void()>& func, int64_t delay_ms);

    /**
     * @brief Attempts to cancel the delayed execution.
     *
     * This function tries to cancel a previously scheduled delayed execution.
     * If the execution has already started or completed or was not scheduled,
     * the cancellation will not have any effect and will return false.
     *
     * @return true if the cancellation was successful, false otherwise.
     */
    bool cancel();

private:
    int     stopped_ = 0;
    event_t stop_;
    event_t done_;
};
