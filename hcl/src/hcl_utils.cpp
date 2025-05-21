#include "hcl_utils.h"

#include <execinfo.h>         // for backtrace, backtrace_symbols
#include <ifaddrs.h>          // for ifaddrs, freeifaddrs, getifaddrs
#include <linux/ethtool.h>    // for ethtool_drvinfo, ETHTOOL_GDRVINFO
#include <linux/sockios.h>    // for SIOCETHTOOL
#include <net/if.h>           // for ifreq, ifr_data, ifr_name
#include <netinet/in.h>       // for IPPROTO_IP, sockaddr_in
#include <signal.h>           // for sigaction, sa_handler, sigemptyset
#include <cstdint>            // for uint64_t, uint32_t
#include <cstdlib>            // for free
#include <sys/ioctl.h>        // for ioctl
#include <sys/types.h>        // for off_t
#include <string>             // for string, allocator, basic_string
#include <utility>            // for pair
#include "hlthunk.h"          // for hlthunk_host_memory_map, hlthunk_mem...
#include "hcl_log_manager.h"  // for LOG_*

std::array<int, (unsigned)HLLOG_ENUM_TYPE_NAME::LOG_MAX> g_logContext = {};

/* in order to prevent physical pages mapped to device from being swapped, thus causing MMU mapping problems, need to
   add madvise flag to those pages.
   for more details see https://jira.habana-labs.com/browse/SW-49789 */
void* alloc_mem_to_be_mapped_to_device(size_t length, void* addr, int prot, int flags, int fd, off_t offset)
{
    // need to allocate page-aligned page-sized memory
    void* hostAddr = mmap(addr, length, prot, flags, fd, offset);

    VERIFY(madvise(hostAddr, length, MADV_DONTFORK) == 0,
           "errno={}, hostAddr=0x{:x}, length={}",
           std::strerror(errno),
           (uint64_t)hostAddr,
           length);
    return hostAddr;
}

void* alloc_and_map_to_device(size_t    length,
                              uint64_t& deviceHandle,
                              int       deviceFd,
                              void*     addr,
                              int       prot,
                              int       flags,
                              int       fd,
                              off_t     offset)
{
    void* hostAddr = alloc_mem_to_be_mapped_to_device(length, addr, prot, flags, fd, offset);
    VERIFY(deviceHandle = hlthunk_host_memory_map(deviceFd, hostAddr, 0, length),
           "hostAddr=0x{:x}, length={}",
           (uint64_t)hostAddr,
           length);
    return hostAddr;
}

void free_mem_mapped_to_device(void* hostAddr, int length, uint64_t deviceHandle, int fd)
{
    if (deviceHandle && fd != -1)
    {
        int rc = hlthunk_memory_unmap(fd, deviceHandle);
        VERIFY(rc == 0, "hlthunk_memory_unmap() failed: {}", rc);
    }

    munmap(hostAddr, length);
}

void getHclVersion(char* pVersion, const unsigned len)
{
    const std::string version =
        fmt::format("{}.{}.{}", HL_DRIVER_MAJOR, HL_DRIVER_MINOR, HL_DRIVER_PATCHLEVEL);

    std::strncpy(pVersion, version.c_str(), len);
    if (version.length() >= len)
    {
        pVersion[len - 1] = '\0';
    }
}

void dumpStack([[maybe_unused]] int s)
{
    constexpr int numEntries = 128;

    void*  entries[numEntries];
    size_t size     = backtrace(entries, numEntries);
    char** messages = (char**)NULL;
    messages        = backtrace_symbols(entries, size);
    if (messages)
    {
        LOG_ERR(HCL, "Backtrace:");
        for (unsigned i = 0; i < size; i++)
        {
            LOG_ERR(HCL, "{}", messages[i]);
        }
        free(messages);
    }
}

// Function to retrieve and return memory information
std::string getMemoryInfo()
{
    // Path to the meminfo file
    const std::string meminfoPath = "/proc/meminfo";
    // Open the meminfo file
    std::ifstream meminfo(meminfoPath);
    // Check if the file was successfully opened
    if (!meminfo.good())
    {
        return "Unable to read memory information";
    }

    // Variables to store the total, free, and used memory values
    std::string line, totalMemory, freeMemory, usedMemory, availableMemory;
    // Read the meminfo file line by line
    while (std::getline(meminfo, line))
    {
        // Check if the line contains the total memory value
        if (line.find("MemTotal:") != std::string::npos)
        {
            totalMemory = line;
        }
        // Check if the line contains the free memory value
        else if (line.find("MemFree:") != std::string::npos)
        {
            freeMemory = line;
        }

        // Check if the line contains the available memory value
        else if (line.find("MemAvailable:") != std::string::npos)
        {
            availableMemory = line;
        }
    }
    // Close the meminfo file
    meminfo.close();

    // Extract the memory values from the strings
    int                totalKB = 0, freeKB = 0, availableKB = 0;
    std::istringstream totalStream(totalMemory);
    totalStream >> totalMemory >> totalKB;
    std::istringstream freeStream(freeMemory);
    freeStream >> freeMemory >> freeKB;
    std::istringstream availableStream(availableMemory);
    availableStream >> availableMemory >> availableKB;

    // Calculate the used memory value
    int usedKB = totalKB - freeKB;

    // Get the current process ID
    int pid = getpid();

    // Construct the path to the process status file
    std::stringstream statusPath;
    statusPath << "/proc/" << pid << "/status";
    // Open the process status file
    std::ifstream status(statusPath.str());
    // Check if the file was successfully opened
    if (!status.good())
    {
        return "Unable to read process memory information";
    }

    // Variable to store the memory used by the current process
    int processMemory = 0;
    // Read the process status file line by line
    while (std::getline(status, line))
    {
        // Check if the line contains the process memory value
        if (line.find("VmSize:") != std::string::npos)
        {
            std::istringstream memoryStream(line);
            memoryStream >> totalMemory >> processMemory;
            break;
        }
    }
    // Close the process status file
    status.close();

    // Construct and return the memory information string
    std::stringstream result;
    result << "Memory - Total: " << totalKB / 1024 << " MB " << "Used: " << usedKB / 1024 << " MB "
           << "Free: " << freeKB / 1024 << " MB " << "Available: " << availableKB / 1024 << " MB " << "Process[" << pid
           << "]: " << processMemory / 1024 << " MB";
    return result.str();
}

// process memory consumption
#define KB2GB(value) ((value) / 1024 / 1024)  // KB to GB

float getProcMemConsInGB()
{
    // process memory size
    float procMem = 0.0;

    // Path to the current process status file
    const std::string procStatusPath = "/proc/self/status";
    // Open the status file
    std::ifstream procStatus(procStatusPath);
    // Check if the file was successfully opened
    if (!procStatus.good())
    {
        return procMem;
    }

    // status line
    std::string line;

    // Read the procStatus file line by line, search for "VmSize:"
    // line looks like:
    // VmSize:     6112 kB
    while (std::getline(procStatus, line))
    {
        if (line.find("VmSize:") != std::string::npos)
        {
            std::istringstream totalStream(line.data());
            totalStream >> line >> procMem;
            break;
        }
    }
    // Close the procStatus file
    procStatus.close();

    return KB2GB(procMem);
}
bool LogContext::s_logCtxtCfg = GCFG_HCL_LOG_CONTEXT.value();

// Async error variables
static std::atomic<hcclResult_t> g_globalDfaStatus = hcclSuccess;
static std::string               g_globalAsyncErrorMessage;
static std::mutex                g_globalAsyncErrorMessageMutex;

hcclResult_t getGlobalDfaStatus()
{
    return g_globalDfaStatus.load();
}

void setGlobalDfaStatus(hcclResult_t status)
{
    g_globalDfaStatus = status;
}

std::string getGlobalAsyncErrorStatusMessage()
{
    std::lock_guard lock(g_globalAsyncErrorMessageMutex);
    std::string     msg = g_globalAsyncErrorMessage;
    return msg;
}

void setGlobalAsyncErrorMessage(const std::string& errMessage)
{
    std::lock_guard lock(g_globalAsyncErrorMessageMutex);
    g_globalAsyncErrorMessage = errMessage;
}
