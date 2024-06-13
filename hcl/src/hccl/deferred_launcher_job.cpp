/******************************************************************************
 * Copyright (C) 2021 Habana Labs, Ltd. an Intel Company
 * All Rights Reserved.
 *
 * Unauthorized copying of this file or any element(s) within it, via any medium
 * is strictly prohibited.
 * This file contains Habana Labs, Ltd. proprietary and confidential information
 * and is subject to the confidentiality and license agreements under which it
 * was provided.
 *
 ******************************************************************************/

#include "deferred_launcher_job.h"
#include <utility>          // for move
#include "hcl_log_manager.h"  // for LOG_ERR
#include "hcl_utils.h"      // for LogMessage, LOG, _TF_LOG_ERROR

deferred_launcher_job::deferred_launcher_job() : quit_requested_ {false}, worker_ {[this] { do_work(); }}
{
    // Make sure the thread is ready.
    assure_ready();
}

deferred_launcher_job::~deferred_launcher_job()
{
    request_quit();

    if (worker_.joinable())
    {
        worker_.join();
    }
}

void deferred_launcher_job::request_task(std::function<void()> fn)
{
    {
        std::lock_guard<std::mutex> lock {mutex_};
        tasks_.push_back(std::move(fn));
    }
    cv_.notify_all();
}

void deferred_launcher_job::request_quit()
{
    {
        std::lock_guard<std::mutex> lock {mutex_};
        quit_requested_ = true;
    }
    cv_.notify_all();
}

void deferred_launcher_job::assure_ready()
{
    std::atomic<bool> ready {false};
    request_task([&ready] { ready = true; });

    // Wait for 'ready'. Spin over a busy-loop for sheer simplicity.
    while (!ready)
    {
        std::this_thread::yield();
    }
}

void deferred_launcher_job::do_work()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock {mutex_};

        if (quit_requested_)
        {
            break;
        }

        cv_.wait(lock, [this] { return !tasks_.empty() || quit_requested_; });

        if (quit_requested_)
        {
            break;
        }

        if (!tasks_.empty())
        {
            auto fetched_tasks = std::move(tasks_);
            tasks_             = {};
            lock.unlock();

            for (auto& task : fetched_tasks)
            {
                task();
            }
        }
    }

    std::unique_lock<std::mutex> lock {mutex_};
    if (!tasks_.empty())
    {
        LOG_HCL_ERR(HCL, "Stopping deferred_launcher_job thread while there are tasks enqueued.");
    }
}
