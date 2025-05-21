#pragma once

#define NOW std::chrono::steady_clock::now

#define set_expired(_sec) auto __expired__ = NOW() + std::chrono::seconds(_sec)
#define is_expired()      (NOW() >= __expired__)

#define wait_sleep 100000  // usec  - 0.1 Seconds
#define wait_condition(cond, timeout_sec, reason)                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        set_expired((timeout_sec));                                                                                    \
        while (!(cond))                                                                                                \
        {                                                                                                              \
            if (is_expired())                                                                                          \
            {                                                                                                          \
                COORD_ERR("operation: {} timeout ({}) expired while waiting for: " #cond, timeout_sec, reason);        \
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
        COORD_ERR(#func " returned with error. ({}) {}", errno, strerror(errno));                                      \
        return false;                                                                                                  \
    }

#define SYS_FUNC_CALL(func)                                                                                            \
    if (func == -1)                                                                                                    \
    {                                                                                                                  \
        COORD_WRN(#func " returned with error. ({}) {}", errno, strerror(errno));                                      \
    }

#define _DEF_IMPL_                                                                                                     \
    {                                                                                                                  \
        COORD_LOG("[ default(empty) implementation ]");                                                                \
    }

#ifndef LOCAL_BUILD

#include "hcl_utils.h"
#include "hcl_sockaddr.h"

#define COORD_LOG(FMT, ...) LOG_HCL_TRACE(HCL_COORD, FMT, ##__VA_ARGS__)
#define COORD_DBG(FMT, ...) LOG_HCL_DEBUG(HCL_COORD, FMT, ##__VA_ARGS__)
#define COORD_ERR(FMT, ...) LOG_HCL_ERR(HCL_COORD, FMT, ##__VA_ARGS__)
#define COORD_INF(FMT, ...) LOG_HCL_INFO(HCL_COORD, FMT, ##__VA_ARGS__)
#define COORD_CRT(FMT, ...) LOG_HCL_CRITICAL(HCL_COORD, FMT, ##__VA_ARGS__)
#define COORD_WRN(FMT, ...) LOG_HCL_WARN(HCL_COORD, FMT, ##__VA_ARGS__)

#endif
