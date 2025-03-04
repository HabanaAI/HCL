#pragma once

#include <cstdint>  // for uint*
#include <atomic>
#include <condition_variable>
#include <mutex>

#include "hccl_communicator.h"  // for hccl_communicator
#include "hcl_log_manager.h"    // for LOG_*
#include "hcl_utils.h"          // for LOG_HCL*, g_status, g_dfaPhase

// DFA vars
extern DfaPhase   g_dfaPhase;
extern std::mutex g_dfaMutex;

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

#define HCCL_CHECK_STOP_SR_API_COMM_UNTIL(hcclComm)                                                                    \
    {                                                                                                                  \
        if (g_faultsCheckStopApi.load())                                                                               \
        {                                                                                                              \
            hcclComm->checkFaultToleranceStopCommSendRecvApiUntil();                                                   \
        }                                                                                                              \
    }

#define HCCL_TRY                                                                                                       \
    if (g_dfaPhase == DfaPhase::STARTED)                                                                               \
    {                                                                                                                  \
        std::unique_lock<std::mutex> lck(g_dfaMutex); /* hold api until dfa finished collecting info */                \
    }                                                                                                                  \
    if (g_status != hcclSuccess)                                                                                       \
    {                                                                                                                  \
        return g_status;                                                                                               \
    }                                                                                                                  \
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
