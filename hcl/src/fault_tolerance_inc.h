#pragma once

#include "hcl_utils.h"

#define COMM_ID_FMT     "commId {} uniqId {} "
#define COMM_ID_FMT_HDR "---commId: {} ---uniqId: {} "

#define HLFT_TRC(...)                                                                                                  \
    {                                                                                                                  \
        LOG_HCL_TRACE(HCL, ##__VA_ARGS__);                                                                             \
        LOG_HCL_TRACE(HCL_FAILOVER, ##__VA_ARGS__);                                                                    \
    }

#define HLFT_COMM_TRC(FMT, CommIds, ...) HLFT_TRC(COMM_ID_FMT FMT, CommIds.commId, CommIds.commIdPort, ##__VA_ARGS__);

#define HLFT_DBG(...)                                                                                                  \
    {                                                                                                                  \
        LOG_HCL_DEBUG(HCL, ##__VA_ARGS__);                                                                             \
        LOG_HCL_DEBUG(HCL_FAILOVER, ##__VA_ARGS__);                                                                    \
    }

#define HLFT_COMM_HDR_DBG(FMT, CommIds, ...)                                                                           \
    HLFT_DBG(COMM_ID_FMT_HDR FMT, CommIds.commId, CommIds.commIdPort, ##__VA_ARGS__);

#define HLFT_COMM_DBG(FMT, CommIds, ...) HLFT_DBG(COMM_ID_FMT FMT, CommIds.commId, CommIds.commIdPort, ##__VA_ARGS__);

#define HLFT_ERR(...)                                                                                                  \
    {                                                                                                                  \
        LOG_HCL_ERR(HCL, ##__VA_ARGS__);                                                                               \
        LOG_HCL_ERR(HCL_FAILOVER, ##__VA_ARGS__);                                                                      \
    }

#define HLFT_COMM_HDR_ERR(FMT, CommIds, ...)                                                                           \
    HLFT_ERR(COMM_ID_FMT_HDR FMT, CommIds.commId, CommIds.commIdPort, ##__VA_ARGS__);

#define HLFT_COMM_ERR(FMT, CommIds, ...) HLFT_ERR(COMM_ID_FMT FMT, CommIds.commId, CommIds.commIdPort, ##__VA_ARGS__);

#define HLFT_INF(...)                                                                                                  \
    {                                                                                                                  \
        LOG_HCL_INFO(HCL, ##__VA_ARGS__);                                                                              \
        LOG_HCL_INFO(HCL_FAILOVER, ##__VA_ARGS__);                                                                     \
    }

#define HLFT_COMM_HDR_INF(FMT, CommIds, ...)                                                                           \
    HLFT_INF(COMM_ID_FMT_HDR FMT, CommIds.commId, CommIds.commIdPort, ##__VA_ARGS__);

#define HLFT_COMM_INF(FMT, CommIds, ...) HLFT_INF(COMM_ID_FMT FMT, CommIds.commId, CommIds.commIdPort, ##__VA_ARGS__);

#define HLFT_API_INF(...)                                                                                              \
    {                                                                                                                  \
        LOG_HCL_INFO(HCL_API, ##__VA_ARGS__);                                                                          \
        LOG_HCL_INFO(HCL_FAILOVER, ##__VA_ARGS__);                                                                     \
    }

#define HLFT_API_COMM_HDR_INF(FMT, CommIds, ...)                                                                       \
    HLFT_API_INF(COMM_ID_FMT_HDR FMT, CommIds.commId, CommIds.commIdPort, ##__VA_ARGS__);

#define HLFT_API_COMM_INF(FMT, CommIds, ...)                                                                           \
    HLFT_API_INF(COMM_ID_FMT FMT, CommIds.commId, CommIds.commIdPort, ##__VA_ARGS__);

#define HLFT_CRT(...)                                                                                                  \
    {                                                                                                                  \
        LOG_HCL_CRITICAL(HCL, ##__VA_ARGS__);                                                                          \
        LOG_HCL_CRITICAL(HCL_FAILOVER, ##__VA_ARGS__);                                                                 \
    }

#define HLFT_WRN(...)                                                                                                  \
    {                                                                                                                  \
        LOG_HCL_WARN(HCL, ##__VA_ARGS__);                                                                              \
        LOG_HCL_WARN(HCL_FAILOVER, ##__VA_ARGS__);                                                                     \
    }
