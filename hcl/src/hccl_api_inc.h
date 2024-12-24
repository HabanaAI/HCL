#pragma once

#include <cstdint>  // for uint*
#include <atomic>
#include <condition_variable>
#include <mutex>

#include "hcl_log_manager.h"  // for LOG_*
#include "hcl_utils.h"        // for LOG_HCL*, g_status, g_dfaPhase

// DFA vars
extern DfaPhase   g_dfaPhase;
extern std::mutex g_dfaMutex;

// Fault tolerance vars
extern std::atomic<bool>       g_faultsCheckStopApi;
extern std::atomic<bool>       g_faultsStopAllApi;
extern std::condition_variable g_faultsStopAllApiCv;
extern std::mutex              g_faultsStopAllApiMutex;

#define HCCL_TRY                                                                                                       \
    if (g_dfaPhase == DfaPhase::STARTED)                                                                               \
    {                                                                                                                  \
        std::unique_lock<std::mutex> lck(g_dfaMutex); /* hold api until dfa finished collecting info */                \
    }                                                                                                                  \
    if (g_status != hcclSuccess)                                                                                       \
    {                                                                                                                  \
        return g_status;                                                                                               \
    }                                                                                                                  \
    if (g_faultsCheckStopApi.load())                                                                                   \
    {                                                                                                                  \
        LOG_DEBUG(HCL_FAILOVER, "{}: Stop API check", __func__);                                                       \
        std::unique_lock<std::mutex> lk(g_faultsStopAllApiMutex);                                                      \
        LOG_DEBUG(HCL_FAILOVER, "{}: Before CV wait", __func__);                                                       \
        g_faultsStopAllApiCv.wait(lk, [] { return !g_faultsStopAllApi; }); /* Block if g_faultsStopAllApi is true */   \
        LOG_DEBUG(HCL_FAILOVER, "{}: After CV wait, g_faultsCheckStopApi={}", __func__, g_faultsCheckStopApi.load());  \
    } /* of g_faultsCheckStopApi check */                                                                              \
    try                                                                                                                \
    {
#define HCCL_API_EXIT(status)                                                                                          \
    if (g_dfaPhase == DfaPhase::STARTED)                                                                               \
    {                                                                                                                  \
        std::unique_lock<std::mutex> lck(g_dfaMutex); /* hold api until dfa finished collecting info */                \
    }                                                                                                                  \
    return status;                                                                                                     \
    } /* of try */                                                                                                     \
    catch (hcl::VerifyException & e)                                                                                   \
    {                                                                                                                  \
        if (g_dfaPhase == DfaPhase::STARTED)                                                                           \
        {                                                                                                              \
            std::unique_lock<std::mutex> lck(g_dfaMutex); /* hold api until dfa finished collecting info */            \
        }                                                                                                              \
        LOG_CRITICAL(HCL, "{} returned {} with exception: {}", __FUNCTION__, g_status, e.what());                      \
        return g_status;                                                                                               \
    };

#define HCL_API_EXIT(status)                                                                                           \
    if (g_dfaPhase == DfaPhase::STARTED)                                                                               \
    {                                                                                                                  \
        std::unique_lock<std::mutex> lck(g_dfaMutex); /* hold api until dfa finished collecting info */                \
    }                                                                                                                  \
    return status;                                                                                                     \
    } /* of try */                                                                                                     \
    catch (hcl::VerifyException & e)                                                                                   \
    {                                                                                                                  \
        if (g_dfaPhase == DfaPhase::STARTED)                                                                           \
        {                                                                                                              \
            std::unique_lock<std::mutex> lck(g_dfaMutex); /* hold api until dfa finished collecting info */            \
        }                                                                                                              \
        LOG_CRITICAL(HCL, "{} returned {} with exception: {}", __FUNCTION__, g_status, e.what());                      \
        return g_status;                                                                                               \
    };

#define HCL_API_LOG_ENTRY(msg, ...) LOG_INFO(HCL_API, "{}: " msg, __func__, ##__VA_ARGS__);
