
#include "delayed_exec.h"
#include "fault_tolerance_inc.h"
#include "hcl_utils.h"

delayed_exec_t::delayed_exec_t() : done_(true) {};

delayed_exec_t::~delayed_exec_t()
{
    cancel();
};

void delayed_exec_t::delay_execution(const std::function<void()>& func, int64_t delay_ms)
{
    VERIFY(done_.signaled(), "previous execution is not done");

    stop_.reset();
    done_.reset();

    std::thread([=]() {
        HLFT_TRC("delaying for {} ms", delay_ms);
        if (stop_.wait(delay_ms))
        {
            // stop_ was signaled while we've been waiting (i.e. cancel() was called)
            stopped_ = 1;
        }
        else
        {
            // timeout occurred
            HLFT_TRC("executing delayed func");
            func();
        }
        done_.signal();
    }).detach();
}

// TRUE  - successfully canceled existing delayed execution.
// FALSE - nothing to cancel. (delayed execution was not defined(or was cancelled) or it was already completed)
bool delayed_exec_t::cancel()
{
    stop_.signal();
    done_.wait();
    return (stopped_-- == 1);
}
