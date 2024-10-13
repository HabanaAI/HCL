#include "infra/hcl_affinity_manager.h"

#include <sched.h>            // for sched_setaffinity, cpu_set_t, CPU_ZERO
#include <cstring>            // for strerror
#include <sys/sysinfo.h>      // for get_nprocs
#include <unistd.h>           // for getpid
#include <cstdint>            // for uint32_t, uint8_t
#include <vector>             // for vector
#include <cerrno>             // for errno
#include "hcl_global_conf.h"  // for GCFG_USE_CPU_AFFINITY
#include "hcl_log_manager.h"  // for LOG_*
#include "hcl_utils.h"

struct HclAffinityManager
{
    bool m_shouldPinThreads = false;

    std::vector<uint32_t> m_priorityCpu;
    std::vector<uint32_t> m_normalCpu;

    uint8_t   m_priorityThreadsRequired = 0;
    cpu_set_t m_normalCpuMask;
};

static HclAffinityManager g_affinityManager;

void initializeCpuPinning(uint8_t priorityThreadsCount)
{
    g_affinityManager.m_priorityThreadsRequired = priorityThreadsCount;

    uint32_t  cpuCount = get_nprocs();
    cpu_set_t set;

    // Get affinity mask of the current process
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) != 0)
    {
        LOG_WARN(HCL, "sched_getaffinity failed (errno: {}). Not pinning threads...", strerror(errno));
        g_affinityManager.m_shouldPinThreads = false;
        return;
    }

    g_affinityManager.m_shouldPinThreads = GCFG_USE_CPU_AFFINITY.value();
    if (!g_affinityManager.m_shouldPinThreads)
    {
        LOG_INFO(HCL, "sched_getaffinity wasn't set. Not pinning threads...");
        return;
    }

    int j = 0;
    CPU_ZERO(&g_affinityManager.m_normalCpuMask);
    for (uint32_t cpuId = 0; cpuId < cpuCount; cpuId++)
    {
        if (CPU_ISSET(cpuId, &set))
        {
            LOG_INFO(HCL, "Process {} CPU {} is set", getpid(), cpuId);
            if (j < g_affinityManager.m_priorityThreadsRequired)
            {
                g_affinityManager.m_priorityCpu.push_back(cpuId);
            }
            else
            {
                g_affinityManager.m_normalCpu.push_back(cpuId);
                CPU_SET(cpuId, &g_affinityManager.m_normalCpuMask);
            }
            j++;
        }
    }

    if (g_affinityManager.m_normalCpu.size() == 0)
    {
        LOG_WARN(HCL, "Not enough CPU cores provided for HCL. Not pinning threads...");
        g_affinityManager.m_shouldPinThreads = false;
        g_affinityManager.m_priorityCpu.clear();
    }
    else
    {
        LOG_INFO(HCL, "Setting main thread to run on remaining threads...");
        sched_setaffinity(0, sizeof(g_affinityManager.m_normalCpuMask), &g_affinityManager.m_normalCpuMask);
    }
}

void HclThread::setCpuAffinity()
{
    VERIFY(m_threadType <= eHCLNormalThread);

    if (!g_affinityManager.m_shouldPinThreads) return;

    if (m_threadType != eHCLNormalThread)
    {
        VERIFY(!g_affinityManager.m_priorityCpu.empty(),
               "tried to create priority thread but there aren't any available!");
        uint32_t cpuId = g_affinityManager.m_priorityCpu[m_threadType];

        LOG_HCL_INFO(HCL,
                     "Setting CPU {} for priority thread {}",
                     cpuId,
                     std::hash<std::thread::id> {}(std::this_thread::get_id()));
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cpuId, &set);
        sched_setaffinity(0, sizeof(set), &set);
    }
    else
    {
        LOG_HCL_INFO(HCL,
                     "Setting thread {} to run on remaining threads...",
                     std::hash<std::thread::id> {}(std::this_thread::get_id()));
        sched_setaffinity(0, sizeof(g_affinityManager.m_normalCpuMask), &g_affinityManager.m_normalCpuMask);
    }
}
