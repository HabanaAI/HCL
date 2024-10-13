#pragma once

#define NOW std::chrono::steady_clock::now

#define set_expired(_sec) auto __expired__ = NOW() + std::chrono::seconds(_sec)
#define is_expired()      (NOW() >= __expired__)

#define wait_sleep 100000  // usec  - 0.1 Seconds
#define wait_condition(cond, timeout_sec)                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        set_expired((timeout_sec));                                                                                    \
        while (!(cond))                                                                                                \
        {                                                                                                              \
            if (is_expired())                                                                                          \
            {                                                                                                          \
                HLCP_ERR("timeout ({}) expired while waiting for: " #cond, timeout_sec);                               \
                return false;                                                                                          \
            }                                                                                                          \
            usleep(wait_sleep);                                                                                        \
        }                                                                                                              \
    } while (false)

#define RET_ON_FALSE(func)                                                                                             \
    if (!func) return false
#define RET_ON_ERR(func)                                                                                               \
    if (func == -1)                                                                                                    \
    {                                                                                                                  \
        HLCP_ERR(#func " returned with error. ({}) {}", errno, strerror(errno));                                       \
        return false;                                                                                                  \
    }

#define _DEF_IMPL_                                                                                                     \
    {                                                                                                                  \
        HLCP_LOG("[ default(empty) implementation ]");                                                                 \
    }

#ifndef LOCAL_BUILD

#include "hcl_utils.h"
#include "hcl_sockaddr.h"

#define HLCP_LOG(...) LOG_HCL_TRACE(HCL_COORD, ##__VA_ARGS__)
#define HLCP_DBG(...) LOG_HCL_DEBUG(HCL_COORD, ##__VA_ARGS__)
#define HLCP_ERR(...) LOG_HCL_ERR(HCL_COORD, ##__VA_ARGS__)
#define HLCP_INF(...) LOG_HCL_INFO(HCL_COORD, ##__VA_ARGS__)
#define HLCP_CRT(...) LOG_HCL_CRITICAL(HCL_COORD, ##__VA_ARGS__)
#define HLCP_WRN(...) LOG_HCL_WARN(HCL_COORD, ##__VA_ARGS__)

#endif