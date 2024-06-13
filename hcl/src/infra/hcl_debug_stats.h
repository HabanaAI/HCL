#pragma once

#include <string>
#include <unordered_map>
#include <map>
#include <list>
#include <thread>
#include <chrono>
#include <mutex>
#include <cstdint>  // for int64_t, uint64_t
#include "hcl_global_conf.h"  // for GCFG...

enum debugStatsLevel
{
    DEBUG_STATS_OFF    = 0,  // Off
    DEBUG_STATS_LOW    = 1,  // Minimal proactor/submitter statistic (almost no performance impact)
    DEBUG_STATS_MEDIUM = 2,  // Add more submitter details (low performance impact)
    DEBUG_STATS_HIGH   = 3,  // Add CQ and some proactor events statistic (medium performance impact)
    DEBUG_STATS_ALL    = 4   // Add most proactor events statistic (high performance impact)
};

#ifdef __GNUC__
#define AUTO_FUNC_NAME __PRETTY_FUNCTION__
#else
#define AUTO_FUNC_NAME __FUNCTION__
#endif

// Macro for manual code instrumentation
// User must call START and relevant COMPLETE macro in all function exit points
#define HCL_FUNC_INSTRUMENTATION_START(level, funcNameOut)                                                             \
    static std::string funcName;                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (unlikely(GCFG_HCL_DEBUG_STATS_LEVEL.value() >= level))                                                     \
        {                                                                                                              \
            static bool isFirst = true;                                                                                \
            if (isFirst)                                                                                               \
            {                                                                                                          \
                funcName                = AUTO_FUNC_NAME;                                                              \
                size_t startParenthesis = funcName.find('(');                                                          \
                funcName = (startParenthesis != std::string::npos) ? funcName.substr(0, startParenthesis) : funcName;  \
                size_t lastSpaceIndex = funcName.find_last_of(" ");                                                    \
                funcName    = (lastSpaceIndex != std::string::npos) ? funcName.substr(lastSpaceIndex + 1) : funcName;  \
                funcNameOut = funcName;                                                                                \
                isFirst     = false;                                                                                   \
            }                                                                                                          \
            g_dbgStats.startFunc(funcNameOut, g_profilerContextName);                                                  \
        }                                                                                                              \
    } while (false)

#define HCL_FUNC_INSTRUMENTATION_COMPLETE(level, funcNameIn)                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (unlikely(GCFG_HCL_DEBUG_STATS_LEVEL.value() >= level))                                                     \
        {                                                                                                              \
            g_dbgStats.completeFunc(funcNameIn);                                                                       \
        }                                                                                                              \
    } while (false)

// Macro for manual code instrumentation with given func name
// User must call STRING_START and relevant STRING_COMPLETE macro in all function exit points
#define HCL_FUNC_INSTRUMENTATION_STRING_START(level, funcName)                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        if (unlikely(GCFG_HCL_DEBUG_STATS_LEVEL.value() >= level))                                                     \
        {                                                                                                              \
            g_dbgStats.startFunc(funcName, g_profilerContextName);                                                     \
        }                                                                                                              \
    } while (false)

#define HCL_FUNC_INSTRUMENTATION_STRING_COMPLETE(level, origFuncName, replaceFuncName)                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        if (unlikely(GCFG_HCL_DEBUG_STATS_LEVEL.value() >= level))                                                     \
        {                                                                                                              \
            g_dbgStats.completeFunc(origFuncName, replaceFuncName);                                                    \
        }                                                                                                              \
    } while (false)

#define HCL_FUNC_INSTRUMENTATION_STRING_ARGS_COMPLETE(level, origFuncName, replaceFuncName, args, argsSize)            \
    do                                                                                                                 \
    {                                                                                                                  \
        if (unlikely(GCFG_HCL_DEBUG_STATS_LEVEL.value() >= level))                                                     \
        {                                                                                                              \
            g_dbgStats.completeFunc(origFuncName, replaceFuncName, args, argsSize);                                    \
        }                                                                                                              \
    } while (false)

// Macro for automatic function instrumentation
// Need to be placed in function (or code section) start only
// When function (or code section) ends completion will be called automatically
#define HCL_FUNC_INSTRUMENTATION(level)                                                                                \
    static bool        isActive = false;                                                                               \
    static std::string funcName;                                                                                       \
    if (unlikely(GCFG_HCL_DEBUG_STATS_LEVEL.value() >= level))                                                         \
    {                                                                                                                  \
        static bool isFirst = true;                                                                                    \
        if (isFirst)                                                                                                   \
        {                                                                                                              \
            funcName                = AUTO_FUNC_NAME;                                                                  \
            size_t startParenthesis = funcName.find('(');                                                              \
            funcName = (startParenthesis != std::string::npos) ? funcName.substr(0, startParenthesis) : funcName;      \
            size_t lastSpaceIndex = funcName.find_last_of(" ");                                                        \
            funcName = (lastSpaceIndex != std::string::npos) ? funcName.substr(lastSpaceIndex + 1) : funcName;         \
            isFirst  = false;                                                                                          \
        }                                                                                                              \
        isActive = true;                                                                                               \
    }                                                                                                                  \
    HclFuncInstrumentation funcInstrumentation(funcName, isActive);

#define HCL_FUNC_INSTRUMENTATION_STRING(level, string)                                                                 \
    static bool isActive = false;                                                                                      \
    if (unlikely(GCFG_HCL_DEBUG_STATS_LEVEL.value() >= level))                                                         \
    {                                                                                                                  \
        isActive = true;                                                                                               \
    }                                                                                                                  \
    HclFuncInstrumentation funcInstrumentation(string, isActive);

class HclDebugStats
{
private:
    using hcl_clk = std::chrono::high_resolution_clock;
    struct FuncInfo
    {
        bool                active = false;
        hcl_clk::time_point lastStart;
        uint64_t            profilerStart;
        double              totalRunTime = 0;
        const char*         contextName  = nullptr;
        int64_t             runCount     = 0;
    };
    using func_time_map = std::unordered_map<std::string, FuncInfo>;
    class HclThreadDebugStats
    {
    public:
        HclThreadDebugStats();
        ~HclThreadDebugStats();

    public:
        func_time_map   threadWorkingFunc;
        std::thread::id tid;
    };

public:
    HclDebugStats();
    ~HclDebugStats() { printStats(true); };
    void printStats(bool normalExit = false);

    void startFunc(const std::string& funcName, const char* contextName = nullptr);
    void completeFunc(const std::string& funcName,
                      std::string        replaceFuncName = "",
                      const char**       args            = nullptr,
                      size_t             argsSize        = 0);
    void setThreadName(const char* threadName);

private:
    void addLocalFuncStorage(HclThreadDebugStats* thInfo);
    void removeLocalFuncStorage(HclThreadDebugStats* thInfo);

    std::string getThreadName(std::thread::id threadID);
    void        printStuckFunctionInfo(std::string& threadName, const std::string& funcName, FuncInfo& func);
    void printPerformanceStatistic(bool normalExit = false);

    std::map<std::thread::id, func_time_map*> m_workingFunc;
    std::map<std::thread::id, std::string>    m_threadNames;
    std::list<func_time_map>                  m_completedThreadsStatsVec;

    static thread_local HclThreadDebugStats m_threadInfo;

    bool       m_printDone = false;
    std::mutex m_printMutex;

    std::string m_statisticFileName  = "hcl_stats_";  // some uniq id and .csv will be added
};

extern HclDebugStats g_dbgStats;

#define PROFILER_CONTEXT_INIT(contextName)                                                                             \
    profilerContext _profiler_context { contextName }

extern thread_local const char* g_profilerContextName;
class profilerContext
{
public:
    profilerContext(const char* contextName)
    {
        if (GCFG_HCL_DEBUG_STATS_LEVEL.value() >= DEBUG_STATS_LOW)
        {
            g_profilerContextName = contextName;
        }
    }
    ~profilerContext()
    {
        if (GCFG_HCL_DEBUG_STATS_LEVEL.value() >= DEBUG_STATS_LOW)
        {
            g_profilerContextName = nullptr;
        }
    }

    profilerContext(profilerContext&)  = delete;
    profilerContext(profilerContext&&) = delete;
    profilerContext& operator=(profilerContext&) = delete;
    profilerContext& operator=(profilerContext&&) = delete;
};

// automatic function start/stop handler
class HclFuncInstrumentation
{
public:
    HclFuncInstrumentation(std::string& func, bool isActive) : m_func(func), m_isActive(isActive)
    {
        if (m_isActive)
        {
            g_dbgStats.startFunc(func, g_profilerContextName);
        }
    }

    ~HclFuncInstrumentation()
    {
        if (m_isActive)
        {
            g_dbgStats.completeFunc(m_func);
        }
    }

private:
    std::string m_func;
    bool        m_isActive;
};
