#pragma once

#include <cstdint>  // for uint*
#include <atomic>
#include <condition_variable>
#include <mutex>

#include "hccl_communicator.h"  // for hccl_communicator
#include "hcl_log_manager.h"    // for LOG_*
#include "hcl_utils.h"          // for LOG_HCL*, g_status, g_dfaPhase

extern DfaPhase   g_dfaPhase;
extern std::mutex g_dfaMutex;

inline void waitForDfaFinish()
{
    if (g_dfaPhase == DfaPhase::STARTED)
    {
        std::unique_lock<std::mutex> lck(g_dfaMutex); /* hold api until dfa finished collecting info */
    }
}

// Fault tolerance vars
extern std::atomic<bool>       g_faultsCheckStopApi;
extern std::atomic<uint32_t>   g_faultsStopAllApi;
extern std::condition_variable g_faultsStopAllApiCv;
extern std::mutex              g_faultsStopAllApiMutex;

void checkFaultToleranceStopApi();

#define HCCL_CHECK_STOP_API()                                                                                          \
    {                                                                                                                  \
        if (g_faultsCheckStopApi.load())                                                                               \
        {                                                                                                              \
            checkFaultToleranceStopApi();                                                                              \
        }                                                                                                              \
    }

#define HCCL_CHECK_STOP_COLL_API_COMM_UNTIL(hcclComm)                                                                  \
    {                                                                                                                  \
        if (g_faultsCheckStopApi.load())                                                                               \
        {                                                                                                              \
            hcclComm->checkFaultToleranceStopCommCollApiUntil();                                                       \
        }                                                                                                              \
    }

#define HCCL_TRY                                                                                                       \
    waitForDfaFinish();                                                                                                \
    resetLastErrorMessage();                                                                                           \
    if (auto status = getGlobalDfaStatus(); status != hcclSuccess)                                                     \
    {                                                                                                                  \
        return status;                                                                                                 \
    }                                                                                                                  \
    try                                                                                                                \
    {
#define HCCL_API_EXIT(status)                                                                                          \
    waitForDfaFinish();                                                                                                \
    return status;                                                                                                     \
    } /* of try */                                                                                                     \
    catch (hcl::VerifyException & e)                                                                                   \
    {                                                                                                                  \
        waitForDfaFinish();                                                                                            \
        auto gStatus = getGlobalDfaStatus();                                                                           \
        LOG_CRITICAL(HCL, "{} returned {} with exception: {}", HLLOG_FUNC, gStatus, e.what());                         \
        return gStatus;                                                                                                \
    };

#define HCL_API_EXIT(status)                                                                                           \
    waitForDfaFinish();                                                                                                \
    return status;                                                                                                     \
    } /* of try */                                                                                                     \
    catch (hcl::VerifyException & e)                                                                                   \
    {                                                                                                                  \
        waitForDfaFinish();                                                                                            \
        auto gStatus = getGlobalDfaStatus();                                                                           \
        LOG_CRITICAL(HCL, "{} returned {} with exception: {}", HLLOG_FUNC, gStatus, e.what());                         \
        return gStatus;                                                                                                \
    };

#define HCL_API_LOG_ENTRY(msg, ...) LOG_INFO(HCL_API, "{}: " msg, HLLOG_FUNC, ##__VA_ARGS__);
