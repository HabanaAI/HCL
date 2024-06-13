#include "hcl_debug_stats.h"

#include <algorithm>                    // for replace
#include <iostream>                     // for operator<<, basic_ostream
#include <fstream>
#include <ratio>                        // for micro
#include <utility>                      // for pair, move
#include "hcl_global_conf.h"            // for GCFG_HCL_DEBUG_STATS_LEVEL
#include "hcl_log_manager.h"            // for LOG_*
#include "synapse_api.h"                // for synProfilerAddCustomMeasurement
#include <sstream>
#include <hcl_utils.h>  // for VERIFY

HclDebugStats g_dbgStats;
thread_local HclDebugStats::HclThreadDebugStats HclDebugStats::m_threadInfo;
thread_local const char*                        g_profilerContextName;

HclDebugStats::HclThreadDebugStats::HclThreadDebugStats()
{
    tid = std::this_thread::get_id();
    g_dbgStats.addLocalFuncStorage(this);
}

HclDebugStats::HclThreadDebugStats::~HclThreadDebugStats()
{
    g_dbgStats.removeLocalFuncStorage(this);
}

void HclDebugStats::addLocalFuncStorage(HclDebugStats::HclThreadDebugStats* thInfo)
{
    std::unique_lock<std::mutex> lock(m_printMutex);
    m_workingFunc[std::this_thread::get_id()] = &thInfo->threadWorkingFunc;
}

void HclDebugStats::removeLocalFuncStorage(HclDebugStats::HclThreadDebugStats* thInfo)
{
    std::unique_lock<std::mutex> lock(m_printMutex);
    m_completedThreadsStatsVec.emplace_back(std::move(thInfo->threadWorkingFunc));
    m_workingFunc[std::this_thread::get_id()] = &m_completedThreadsStatsVec.back();
}

HclDebugStats::HclDebugStats()
{
    // Read config from env. vars (debug feature, not exposed to user through config)

    uint64_t level = GCFG_HCL_DEBUG_STATS_LEVEL.value();
    if (!level)
    {
        return;
    }

    if (!GCFG_HCL_DEBUG_STATS_FILE.value().empty())
    {
        m_statisticFileName = GCFG_HCL_DEBUG_STATS_FILE.value();
    }

    m_statisticFileName += "tid_";
    m_statisticFileName += getThreadName(std::this_thread::get_id());
    m_statisticFileName += ".csv";
}

// take function info on start
// recurcive function are not supported for now
void HclDebugStats::startFunc(const std::string& funcName, const char* contextName)
{
    FuncInfo& funcInfo = m_threadInfo.threadWorkingFunc[funcName];
    funcInfo.active    = true;
    funcInfo.lastStart = hcl_clk::now();
    funcInfo.contextName = contextName;
    synProfilerGetCurrentTimeNS(&funcInfo.profilerStart);
}

// take function info on complete
void HclDebugStats::completeFunc(const std::string& origFuncName,
                                 std::string        replaceFuncName,
                                 const char**       args,
                                 size_t             argsSize)
{
    auto it = m_threadInfo.threadWorkingFunc.find(origFuncName);
    VERIFY(it != m_threadInfo.threadWorkingFunc.end(), "funcName={} isn't part of the map", origFuncName);

    auto& funcInfo = m_threadInfo.threadWorkingFunc[origFuncName];
    funcInfo.totalRunTime += std::chrono::duration<double, std::micro>(hcl_clk::now() - funcInfo.lastStart).count();
    funcInfo.active = false;
    funcInfo.runCount++;

    const std::string& usingFuncName = !replaceFuncName.empty() ? replaceFuncName : origFuncName;
    synProfilerAddCustomMeasurementArgsAndThread(usingFuncName.c_str(),
                                                 funcInfo.profilerStart,
                                                 args,
                                                 argsSize,
                                                 funcInfo.contextName);
}

// set thread name
void HclDebugStats::setThreadName(const char* thread_name)
{
    bool should_collect_data = (GCFG_HCL_DEBUG_STATS_LEVEL.value() > DEBUG_STATS_OFF ||
                                std::string(GCFG_HABANA_PROFILE.value()).compare("0") != 0x0);
    if (!should_collect_data) return;

    std::unique_lock<std::mutex> lock(m_printMutex);
    m_threadNames[std::this_thread::get_id()] = thread_name;
}

std::string HclDebugStats::getThreadName(std::thread::id threadID)
{
    std::stringstream nameStr;

    auto threadName = m_threadNames.find(threadID);

    if (threadName != m_threadNames.end())
    {
        nameStr << threadName->second;
    }
    else
    {
        nameStr << threadID;
    }

    return nameStr.str();
}

void HclDebugStats::printStuckFunctionInfo(std::string&             threadName,
                                           const std::string&       funcName,
                                           HclDebugStats::FuncInfo& func)
{
    if (GCFG_HCL_DEBUG_STATS_LEVEL.value() > DEBUG_STATS_OFF && func.active)
    {
        auto endTime        = std::chrono::high_resolution_clock::now();
        auto timeMilli      = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - func.lastStart);
        auto averageRunTime = func.runCount > 0 ? func.totalRunTime / func.runCount : 0;

        LOG_CRITICAL(HCL,
                     "Thread '{}' Func '{}' is still active for {} ms (average {} ms)",
                     threadName,
                     funcName,
                     timeMilli.count(),
                     averageRunTime / 1000);
    }
}

// output statistic in CSV format
// meanwhile in console output
void HclDebugStats::printPerformanceStatistic(bool normalExit)
{
    if (GCFG_HCL_DEBUG_STATS_LEVEL.value() == DEBUG_STATS_OFF) return;

    auto*         out = &std::cout;
    std::ofstream outFfile;

    outFfile.open(m_statisticFileName);

    if (outFfile.good() && outFfile.is_open())
    {
        out = &outFfile;
    }

    *out << "function, call count, total time (microsec), time per call (microsec)" << std::endl;
    func_time_map statFuncMap;
    for (auto& threadFuncs : m_workingFunc)
    {
        for (auto& func : *(threadFuncs.second))
        {
            auto& funcInfo = statFuncMap[func.first];
            funcInfo.runCount += func.second.runCount;
            funcInfo.totalRunTime += func.second.totalRunTime;
        }
    }

    for (auto& func : statFuncMap)
    {
        std::string s = func.first;
        std::replace(s.begin(), s.end(), ',', ';');
        std::stringstream outStr;
        outStr << s << " , " << std::fixed << func.second.runCount << " , " << func.second.totalRunTime << " , "
               << func.second.totalRunTime / func.second.runCount;

        *out << outStr.str() << std::endl;
        if (!normalExit)
        {
            LOG_ERR(HCL, "{}", outStr.str());
        }
    }

    if (outFfile.good() && outFfile.is_open())
    {
        outFfile.close();
    }
}

// print collected info in case of error
// if HclDebugStats destructor executed - program successfully complete, so print performance statistic only if enabled
void HclDebugStats::printStats(bool normalExit)
{
    if (GCFG_HCL_DEBUG_STATS_LEVEL.value() == DEBUG_STATS_OFF) return;

    std::unique_lock<std::mutex> lock(m_printMutex);
    if (m_printDone) return;
    m_printDone = true;

    if (!normalExit)
    {
        hcl::LogManager::instance().set_log_level(hcl::LogManager::LogType::HCL, HLLOG_LEVEL_ERROR);

        for (auto& threadFuncs : m_workingFunc)
        {
            std::string threadName(getThreadName(threadFuncs.first));
            for (auto& func : *(threadFuncs.second))
            {
                printStuckFunctionInfo(threadName, func.first, func.second);
            }
        }
    }
    printPerformanceStatistic(normalExit);
}