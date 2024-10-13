#pragma once

#include <thread>
#include <functional>
#include <pthread.h>  // for pthread_self
#include <cstdint>    // for uint32_t, uint64_t, uint8_t
#include <string>     // for string, allocator
#include <utility>    // for forward
#include "hcl_utils.h"

enum HclThreadType
{
    eHCLProactorThread      = 0,
    eHCLSubmitterThread     = 1,
    eHCLHostSchedulerThread = 2,
    eHCLNormalThread        = 3
};

void initializeCpuPinning(uint8_t priorityThreadsCount);

class HclThread
{
public:
    HclThread() = default;

    /**
     * Initialize a new std::thread.
     *
     * @param myRank the current rank of the device (will be used in a followup patch to set log context)
     * @param threadType the priority of the thread
     * @param f the function to run in a thread
     * @param args the arguments for f
     */
    template<typename Function, typename... Args>
    explicit HclThread(uint32_t myDevice, std::string hostname, HclThreadType threadType, Function&& f, Args&&... args)
    {
        initialize(myDevice, hostname, threadType, f, args...);
    }

    template<typename Function, typename... Args>
    void initialize(uint32_t myDevice, std::string hostname, HclThreadType threadType, Function&& f, Args&&... args)
    {
        m_myDevice   = myDevice;
        m_hostname   = hostname;
        m_threadType = threadType;

        std::function<void()> func = std::bind(std::forward<Function>(f), std::forward<Args>(args)...);
        m_thread                   = std::thread(&HclThread::run, this, func);
    }

    HclThread(HclThread& other)             = delete;
    HclThread(HclThread&& other)            = delete;
    HclThread& operator=(HclThread& other)  = delete;
    HclThread& operator=(HclThread&& other) = delete;

    void join()
    {
        if (m_thread.joinable()) m_thread.join();
    }

private:
    void run(std::function<void()> func)
    {
        setCpuAffinity();
        setLogContext(m_myDevice, m_hostname, (uint64_t)pthread_self());
        func();
    }
    void setCpuAffinity();

    uint32_t      m_myDevice = 0;
    std::string   m_hostname = "";
    std::thread   m_thread;
    HclThreadType m_threadType = eHCLNormalThread;
};
