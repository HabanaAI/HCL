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

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <thread>
#include <mutex>
#include <vector>

// This class runs a single thread, which processes requested tasks.
// It is intended for invoking device_stream::finish_operation issued by memcpy and collective operations done
// callbacks. Thanks to this approach, those done callbacks do not directly acquire device_stream mutex, as it may lead
// to a deadlock on event mutex in stream event manager.
//
class deferred_launcher_job
{
public:
    deferred_launcher_job();
    ~deferred_launcher_job();
    void request_task(std::function<void()> fn);
    void assure_ready();

private:
    void do_work();
    void request_quit();

private:
    std::atomic<bool>                  quit_requested_;
    std::mutex                         mutex_;
    std::condition_variable            cv_;
    std::vector<std::function<void()>> tasks_;
    std::thread                        worker_;  // Must be constructed after all other
};
