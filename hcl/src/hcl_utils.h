#pragma once

#include <arpa/inet.h>       // for inet_ntoa, inet_pton
#include <netinet/ether.h>   // for ether_aton
#include <cerrno>            // for errno
#include <fcntl.h>           // for open, O_RDWR
#include <linux/if_ether.h>  // for ETH_ALEN
#include <netdb.h>           // for gethostbyname, hostent
#include <cstdio>            // for printf, sprintf, sscanf
#include <cstring>           // for strrchr, memcpy
#include <sys/mman.h>        // for PROT_READ, PROT_WRITE, mmap
#include <sys/socket.h>      // for AF_INET
#include <unistd.h>          // for close, usleep, gethostname
#include <algorithm>         // for min
#include <chrono>            // for nanoseconds, steady_clock
#include <cstdint>           // for uint8_t, uint32_t, uint64_t
#include <cstdlib>           // for getenv, size_t, strtoul, NULL
#include <exception>         // for terminate
#include <iomanip>           // for operator<<, setprecision, setw
#include <iostream>          // for operator<<, basic_ostream
#include <fstream>           // for fstream
#include <string>            // for allocator, operator+, string
#include <unordered_set>     // for unordered_set, unordered_set...
#include <vector>            // for vector
#include <future>            // for future
#include "infra/get_class_name.hpp"

#include "hccl_types.h"       // for hcclResult_t
#include "hcl_api_types.h"    // for HCL_CollectiveOp
#include "hccl_types.h"       // for hcclInternalError
#include "hcl_exceptions.h"   // for VerifyException
#include "hcl_global_conf.h"  // for GCFG_HCL_ALIVE_ON_FAILURE
#include "hcl_types.h"        // for HCL_MAC_BYTE_SIZE, SRAM_REDU...
#include "hcl_log_manager.h"  // for LOG_ERR, LogManager, LOG_CRI...

#include "internal/hcl_api.hpp"  // for hclNotifyFailure

#include "version.h"  // for HL_DRIVER_MAJOR, HL_DRIVER_M...

#define IS_DEVICE_GAUDI2(deviceType)   (deviceType == synDeviceGaudi2)
#define IS_DEVICE_GAUDI3(deviceType)   (deviceType == synDeviceGaudi3)
#define IS_DEVICE_GEN2ARCH(deviceType) (deviceType == synDeviceGaudi2 || deviceType == synDeviceGaudi3)
/**
 * @brief Aligns the given base value up to the nearest multiple of the given size
 *
 */
#define _ALIGN_UP(base, size) (((base) + ((size) - 1)) & (~((size) - 1)))
/**
 * @brief Aligns the given base value down to the nearest multiple of the given size
 *
 */
#define _ALIGN_DOWN(base, size) ((base) & (~((size) - 1)))

hcclResult_t getGlobalDfaStatus();
void         setGlobalDfaStatus(hcclResult_t dfaStatus);
std::string  getGlobalAsyncErrorStatusMessage();
void         setGlobalAsyncErrorMessage(const std::string& errMessage);

/**
 * LOG_HCL_INFO(HCL, "msg") will produce a log line such as:
 * [info] MyClass::run msg
 */
#define _HCL_LOG_(KIND, log_type, msg, ...)                                                                            \
    LOG_##KIND(log_type,                                                                                               \
               "{:{}}{}::{} " msg,                                                                                     \
               "",                                                                                                     \
               g_logContext[(unsigned)HLLOG_ENUM_TYPE_NAME::log_type],                                                 \
               CLASS_NAME,                                                                                             \
               HLLOG_FUNC,                                                                                             \
               ##__VA_ARGS__);

#define LOG_HCL_TRACE(log_type, msg, ...)    _HCL_LOG_(TRACE, log_type, msg, ##__VA_ARGS__)
#define LOG_HCL_DEBUG(log_type, msg, ...)    _HCL_LOG_(DEBUG, log_type, msg, ##__VA_ARGS__)
#define LOG_HCL_INFO(log_type, msg, ...)     _HCL_LOG_(INFO, log_type, msg, ##__VA_ARGS__)
#define LOG_HCL_WARN(log_type, msg, ...)     _HCL_LOG_(WARN, log_type, msg, ##__VA_ARGS__)
#define LOG_HCL_ERR(log_type, msg, ...)      _HCL_LOG_(ERR, log_type, msg, ##__VA_ARGS__)
#define LOG_HCL_CRITICAL(log_type, msg, ...) _HCL_LOG_(CRITICAL, log_type, msg, ##__VA_ARGS__)

extern std::array<int, (unsigned)HLLOG_ENUM_TYPE_NAME::LOG_MAX> g_logContext;

class LogContext
{
public:
    LogContext(HLLOG_ENUM_TYPE_NAME logType) : m_logTypeIndex((unsigned)logType)
    {
        if (likely(LogContext::s_logCtxtCfg))
        {
            g_logContext[m_logTypeIndex] += 4;
        }
    }
    ~LogContext()
    {
        if (likely(LogContext::s_logCtxtCfg))
        {
            g_logContext[m_logTypeIndex] -= 4;
        }
    }

    LogContext(LogContext&)             = delete;
    LogContext(LogContext&&)            = delete;
    LogContext& operator=(LogContext&)  = delete;
    LogContext& operator=(LogContext&&) = delete;

private:
    int         m_logTypeIndex;
    static bool s_logCtxtCfg;
};

// One level of macro indirection is required in order to resolve __COUNTER__,
// and get varname1 instead of varname__COUNTER__.
#define CONCAT(a, b)               CONCAT_INNER(a, b)
#define CONCAT_INNER(a, b)         a##b
#define UNIQUE_NAME(base)          CONCAT(base, __COUNTER__)
#define LOG_CONTEXT_INIT(log_type) LogContext UNIQUE_NAME(_log_context) {HLLOG_ENUM_TYPE_NAME::log_type};
#define LOG_HCL_CONTEXT_TRACE(log_type, msg, ...)                                                                      \
    _HCL_LOG_(TRACE, log_type, msg, ##__VA_ARGS__);                                                                    \
    LOG_CONTEXT_INIT(log_type)
#define LOG_HCL_CONTEXT_DEBUG(log_type, msg, ...)                                                                      \
    _HCL_LOG_(DEBUG, log_type, msg, ##__VA_ARGS__);                                                                    \
    LOG_CONTEXT_INIT(log_type)
#define LOG_HCL_CONTEXT_INFO(log_type, msg, ...)                                                                       \
    _HCL_LOG_(INFO, log_type, msg, ##__VA_ARGS__);                                                                     \
    LOG_CONTEXT_INIT(log_type)
#define LOG_HCL_CONTEXT_WARN(log_type, msg, ...)                                                                       \
    _HCL_LOG_(WARN, log_type, msg, ##__VA_ARGS__);                                                                     \
    LOG_CONTEXT_INIT(log_type)
#define LOG_HCL_CONTEXT_ERR(log_type, msg, ...)                                                                        \
    _HCL_LOG_(ERR, log_type, msg, ##__VA_ARGS__);                                                                      \
    LOG_CONTEXT_INIT(log_type)
#define LOG_HCL_CONTEXT_CRITICAL(log_type, msg, ...)                                                                   \
    _HCL_LOG_(CRITICAL, log_type, msg, ##__VA_ARGS__);                                                                 \
    LOG_CONTEXT_INIT(log_type)

#define LOG_HCL_HEADER(log_type) LOG_TRACE(HCL, "{}::{}", CLASS_NAME, __func__);

/**
 * LOG_HCL_EVENT_INFO(HCL, event, "msg") will produce a log line such as:
 * [info] MyClass::run(->1) eventID(2) queueOffset(3) msg
 */
#define LOG_HCL_EVENT_TRACE(log_type, event, msg, ...)                                                                 \
    LOG_TRACE(log_type,                                                                                                \
              "{}::{}(->{}) eventID({}) queueOffset({}) " msg,                                                         \
              event->name(),                                                                                           \
              HLLOG_FUNC,                                                                                              \
              event->m_remoteRank,                                                                                     \
              event->eventId(),                                                                                        \
              event->m_physicalQueueOffset,                                                                            \
              ##__VA_ARGS__);

#define LOG_HCL_EVENT_DEBUG(log_type, event, msg, ...)                                                                 \
    LOG_DEBUG(log_type,                                                                                                \
              "{}::{}(->{}) eventID({}) queueOffset({}) " msg,                                                         \
              event->name(),                                                                                           \
              HLLOG_FUNC,                                                                                              \
              event->m_remoteRank,                                                                                     \
              event->eventId(),                                                                                        \
              event->m_physicalQueueOffset,                                                                            \
              ##__VA_ARGS__);

#define LOG_HCL_EVENT_INFO(log_type, event, msg, ...)                                                                  \
    LOG_INFO(log_type,                                                                                                 \
             "{}::{}(->{}) eventID({}) queueOffset({}) " msg,                                                          \
             event->name(),                                                                                            \
             HLLOG_FUNC,                                                                                               \
             event->m_remoteRank,                                                                                      \
             event->eventId(),                                                                                         \
             event->m_physicalQueueOffset,                                                                             \
             ##__VA_ARGS__);

#define LOG_HCL_EVENT_WARN(log_type, event, msg, ...)                                                                  \
    LOG_WARN(log_type,                                                                                                 \
             "{}::{}(->{}) eventID({}) queueOffset({}) " msg,                                                          \
             event->name(),                                                                                            \
             HLLOG_FUNC,                                                                                               \
             event->m_remoteRank,                                                                                      \
             event->eventId(),                                                                                         \
             event->m_physicalQueueOffset,                                                                             \
             ##__VA_ARGS__);

#define LOG_HCL_EVENT_ERR(log_type, event, msg, ...)                                                                   \
    LOG_ERR(log_type,                                                                                                  \
            "{}::{}(->{}) eventID({}) queueOffset({}) " msg,                                                           \
            event->name(),                                                                                             \
            HLLOG_FUNC,                                                                                                \
            event->m_remoteRank,                                                                                       \
            event->eventId(),                                                                                          \
            event->m_physicalQueueOffset,                                                                              \
            ##__VA_ARGS__);

#define LOG_HCL_EVENT_CRITICAL(log_type, event, msg, ...)                                                              \
    LOG_CRITICAL(log_type,                                                                                             \
                 "{}::{}(->{}) eventID({}) queueOffset({}) " msg,                                                      \
                 event->name(),                                                                                        \
                 __func__,                                                                                             \
                 event->m_remoteRank,                                                                                  \
                 event->eventId(),                                                                                     \
                 event->m_physicalQueueOffset,                                                                         \
                 ##__VA_ARGS__);

#define LOG_LEVEL_RATELIMITTER_CHECK(log_type, rate, loglevel, msg, ...)                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        static uint64_t                                       suppressedCount = 0;                                     \
        std::chrono::high_resolution_clock::time_point        now = std::chrono::high_resolution_clock::now();         \
        static std::chrono::high_resolution_clock::time_point last_hit;                                                \
        static std::string                                    last_msg;                                                \
        if (std::chrono::duration<double, std::milli>(now - last_hit).count() < rate && last_msg.compare(msg) == 0)    \
        {                                                                                                              \
            suppressedCount++;                                                                                         \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            if (suppressedCount > 0)                                                                                   \
            {                                                                                                          \
                HLLOG_TYPED(log_type, loglevel, "suppressed({}) " msg, suppressedCount, ##__VA_ARGS__);                \
                suppressedCount = 0;                                                                                   \
            }                                                                                                          \
            else                                                                                                       \
            {                                                                                                          \
                HLLOG_TYPED(log_type, loglevel, msg, ##__VA_ARGS__);                                                   \
            }                                                                                                          \
            last_hit = now;                                                                                            \
        }                                                                                                              \
        last_msg = msg;                                                                                                \
    } while (false)

#define LOG_TRACE_RATELIMITTER(log_type, rate, msg, ...)                                                               \
    LOG_LEVEL_RATELIMITTER_CHECK(log_type, rate, 0, msg, ##__VA_ARGS__);
#define LOG_DEBUG_RATELIMITTER(log_type, rate, msg, ...)                                                               \
    LOG_LEVEL_RATELIMITTER_CHECK(log_type, rate, 1, msg, ##__VA_ARGS__);
#define LOG_INFO_RATELIMITTER(log_type, rate, msg, ...)                                                                \
    LOG_LEVEL_RATELIMITTER_CHECK(log_type, rate, 2, msg, ##__VA_ARGS__);
#define LOG_WARN_RATELIMITTER(log_type, rate, msg, ...)                                                                \
    LOG_LEVEL_RATELIMITTER_CHECK(log_type, rate, 3, msg, ##__VA_ARGS__);
#define LOG_ERR_RATELIMITTER(log_type, rate, msg, ...)                                                                 \
    LOG_LEVEL_RATELIMITTER_CHECK(log_type, rate, 4, msg, ##__VA_ARGS__);
#define LOG_CRITICAL_RATELIMITTER(log_type, rate, msg, ...)                                                            \
    LOG_LEVEL_RATELIMITTER_CHECK(log_type, rate, 5, msg, ##__VA_ARGS__);

#define LOG_HCL_RATELIMITTER_EVENT_TRACE(log_type, rate, event, msg, ...)                                              \
    LOG_TRACE_RATELIMITTER(log_type,                                                                                   \
                           rate,                                                                                       \
                           "{}::{}(->{}) eventID({}) queueOffset({}) " msg,                                            \
                           event->name(),                                                                              \
                           __func__,                                                                                   \
                           event->m_remoteRank,                                                                        \
                           event->eventId(),                                                                           \
                           event->m_physicalQueueOffset,                                                               \
                           ##__VA_ARGS__);

#define LOG_HCL_RATELIMITTER_EVENT_DEBUG(log_type, rate, event, msg, ...)                                              \
    LOG_DEBUG_RATELIMITTER(log_type,                                                                                   \
                           rate,                                                                                       \
                           "{}::{}(->{}) eventID({}) queueOffset({}) " msg,                                            \
                           event->name(),                                                                              \
                           __func__,                                                                                   \
                           event->m_remoteRank,                                                                        \
                           event->eventId(),                                                                           \
                           event->m_physicalQueueOffset,                                                               \
                           ##__VA_ARGS__);

#define LOG_HCL_RATELIMITTER_EVENT_INFO(log_type, rate, event, msg, ...)                                               \
    LOG_INFO_RATELIMITTER(log_type,                                                                                    \
                          rate,                                                                                        \
                          "{}::{}(->{}) eventID({}) queueOffset({}) " msg,                                             \
                          event->name(),                                                                               \
                          __func__,                                                                                    \
                          event->m_remoteRank,                                                                         \
                          event->eventId(),                                                                            \
                          event->m_physicalQueueOffset,                                                                \
                          ##__VA_ARGS__);

#define LOG_HCL_RATELIMITTER_EVENT_WARN(log_type, rate, event, msg, ...)                                               \
    LOG_WARN_RATELIMITTER(log_type,                                                                                    \
                          rate,                                                                                        \
                          "{}::{}(->{}) eventID({}) queueOffset({}) " msg,                                             \
                          event->name(),                                                                               \
                          __func__,                                                                                    \
                          event->m_remoteRank,                                                                         \
                          event->eventId(),                                                                            \
                          event->m_physicalQueueOffset,                                                                \
                          ##__VA_ARGS__);

#define LOG_HCL_RATELIMITTER_EVENT_ERR(log_type, rate, event, msg, ...)                                                \
    LOG_ERR_RATELIMITTER(log_type,                                                                                     \
                         rate,                                                                                         \
                         "{}::{}(->{}) eventID({}) queueOffset({}) " msg,                                              \
                         event->name(),                                                                                \
                         __func__,                                                                                     \
                         event->m_remoteRank,                                                                          \
                         event->eventId(),                                                                             \
                         event->m_physicalQueueOffset,                                                                 \
                         ##__VA_ARGS__);

#define LOG_HCL_RATELIMITTER_EVENT_CRITICAL(log_type, rate, event, msg, ...)                                           \
    LOG_CRITICAL_RATELIMITTER(log_type,                                                                                \
                              rate,                                                                                    \
                              "{}::{}(->{}) eventID({}) queueOffset({}) " msg,                                         \
                              event->name(),                                                                           \
                              __func__,                                                                                \
                              event->m_remoteRank,                                                                     \
                              event->eventId(),                                                                        \
                              event->m_physicalQueueOffset,                                                            \
                              ##__VA_ARGS__);

#define VERIFY_1(dfa, dfaMsg, condition) VERIFY_2(dfa, dfaMsg, condition, "")
#define VERIFY_2(dfa, dfaMsg, condition, msg)                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if (unlikely(!(condition)))                                                                                    \
        {                                                                                                              \
            std::stringstream _ss;                                                                                     \
            _ss << __FILE__ << ":" << __LINE__ << "(" << __func__ << "): The condition [ " << #condition               \
                << " ] failed. " << msg << " ";                                                                        \
            std::string error = _ss.str();                                                                             \
            std::cerr << error << std::endl;                                                                           \
            LOG_CRITICAL(HCL, "{}", error);                                                                            \
            if (GCFG_HCL_ALIVE_ON_FAILURE.value())                                                                     \
            {                                                                                                          \
                while (1)                                                                                              \
                {                                                                                                      \
                    usleep(10);                                                                                        \
                }                                                                                                      \
            }                                                                                                          \
            else                                                                                                       \
            {                                                                                                          \
                if (!dfa)                                                                                              \
                {                                                                                                      \
                    setGlobalDfaStatus(hcclInternalError);                                                             \
                    setGlobalAsyncErrorMessage(error);                                                                 \
                    throw hcl::VerifyException(error);                                                                 \
                    std::terminate();                                                                                  \
                }                                                                                                      \
                else                                                                                                   \
                {                                                                                                      \
                    hclNotifyFailureV2(DfaErrorCode::hclFailed, 0, dfaMsg);                                            \
                    throw hcl::VerifyException(error);                                                                 \
                    std::terminate();                                                                                  \
                }                                                                                                      \
            }                                                                                                          \
        }                                                                                                              \
    } while (false)

#define VERIFY_n(dfa, dfaMsg, condition, str, ...)                                                                     \
    VERIFY_2(dfa,                                                                                                      \
             dfaMsg,                                                                                                   \
             condition,                                                                                                \
             fmt::format(FMT_COMPILE(str) HLLOG_APPLY_WITH_LEADING_COMMA(HLLOG_DUPLICATE_PARAM, ##__VA_ARGS__)))

/* The following is a macro trick that allows you to write a macro with a varying number of arguments. It's needed
 * because we would like to allow the user to call VERIFY(false) without any arguments, and also allow any number of
 * other arguments.
 * If you need more than 10 format arguments - just add another VERIFY_n() call to the definition of VERIFY(), and
 * add another argument to VERIFY_X (one of the capital alphabets).
 */
#define VERIFY_X(x, A, B, C, D, E, F, G, I, J, K, L, M, FUNC, ...) FUNC

#define VERIFY(...)                            VERIFYX(false, "", __VA_ARGS__)
#define VERIFY_DFA(...)                        VERIFYX(true, "", __VA_ARGS__)
#define VERIFY_DFA_MSG(condition, dfaMsg, ...) VERIFYX(true, dfaMsg, condition, __VA_ARGS__)

#define VERIFYX(dfa, dfaMsg, ...)                                                                                      \
    VERIFY_X(,                                                                                                         \
             ##__VA_ARGS__,                                                                                            \
             VERIFY_n(dfa, dfaMsg, __VA_ARGS__),                                                                       \
             VERIFY_n(dfa, dfaMsg, __VA_ARGS__),                                                                       \
             VERIFY_n(dfa, dfaMsg, __VA_ARGS__),                                                                       \
             VERIFY_n(dfa, dfaMsg, __VA_ARGS__),                                                                       \
             VERIFY_n(dfa, dfaMsg, __VA_ARGS__),                                                                       \
             VERIFY_n(dfa, dfaMsg, __VA_ARGS__),                                                                       \
             VERIFY_n(dfa, dfaMsg, __VA_ARGS__),                                                                       \
             VERIFY_n(dfa, dfaMsg, __VA_ARGS__),                                                                       \
             VERIFY_n(dfa, dfaMsg, __VA_ARGS__),                                                                       \
             VERIFY_n(dfa, dfaMsg, __VA_ARGS__),                                                                       \
             VERIFY_2(dfa, dfaMsg, __VA_ARGS__),                                                                       \
             VERIFY_1(dfa, dfaMsg, __VA_ARGS__), )

inline bool checkReductionOp(hcclRedOp_t reduceOp)
{
    /* No default in switch case to enforce adding new enums */
    switch (reduceOp)
    {
        case hcclSum:
        case hcclMin:
        case hcclMax:
            return true;
        case hcclProd:
        case hcclAvg:
        case hcclOpNone:
            LOG_ERR(HCL, "unsupported reduction op {}", reduceOp);
            return false;
    }

    return false;
}

inline unsigned dataTypeSizeInBytes(hcclDataType_t type, [[maybe_unused]] bool packed = false)
{
    switch (type)
    {
        case hcclInt8:
        case hcclUint8:
            return 1;

        case hcclFloat16:
        case hcclBfloat16:
            return 2;

        case hcclInt32:
        case hcclUint32:
        case hcclFloat32:
            return 4;

        case hcclInt64:
        case hcclUint64:
        case hcclFloat64:
            return 8;

        default:
            VERIFY(false, "Invalid data type {}", type);
            return 0;
    }
}

/**
 * @brief Converts a number of bytes to megabytes, rounding the result to 3 decimal places.
 *
 */
#define B2MB(value) (round((value * 1000.0) / 1000.0) / 1024.0 / 1024)

inline uint64_t sizeToCount(const uint64_t size, const hcclDataType_t dataType)
{
    return size / dataTypeSizeInBytes(dataType);
}

inline bool isEnvExist(const char* s)
{
    return std::getenv(s) != nullptr;
}

inline std::string getEnvStr(const char* s, const std::string& defaultValue = "")
{
    return (isEnvExist(s) ? std::getenv(s) : defaultValue);
}

inline int64_t getEnvInt(const char* s, int64_t val = 0)
{
    return (isEnvExist(s) ? std::stoll(std::getenv(s)) : val);
}

inline std::string ip2str(uint32_t ip)
{
    in_addr addr = {};
    addr.s_addr  = ip;       // s_addr must be in network byte order
    return inet_ntoa(addr);  // --> "10.1.2.3"
}

inline std::string ip2str(uint128_t ip6)
{
    char ips[INET6_ADDRSTRLEN] = {};
    return inet_ntop(AF_INET6, &ip6, ips, sizeof(ips));
}

// Extend std namespace
namespace std
{
template<class Container, class Item>
bool contains(const Container& container, const Item& item)
{
    return container.end() != std::find(container.begin(), container.end(), item);
}
}  // namespace std

template<typename T>
inline std::string int_to_hex(T i)
{
    std::stringstream stream;
    stream << "0x" << std::setfill('0') << std::setw(sizeof(T) * 2) << std::hex << i;
    return stream.str();
}

inline bool isFileExist(const std::string& path)
{
    return std::ifstream(path).good();
}

inline std::string getHLDevice(const int fd)
{
    std::string path = "/proc/self/fd/" + std::to_string(fd);
    // 32 bytes is sufficient to capture "/dev/accel/accel[0-7]"
    const ssize_t max_buf_size = 32;
    char          buf[max_buf_size];

    ssize_t size = readlink(path.c_str(), buf, max_buf_size);
    if (-1 == size)
    {
        LOG_WARN(HCL, "{}: unable to read symlink for path {}", __func__, path);
        return std::string();
    }
    buf[std::min(size, max_buf_size - 1)] = '\0';
    return std::string(buf);
}

inline uint64_t parseMac(const std::string& in)
{
    struct ether_addr* addr = ether_aton(in.c_str());
    if (addr == NULL)
    {
        return -1;
    }

    uint64_t result = 0;

    static_assert(sizeof(result) >= ETH_ALEN);

    memcpy(&result, addr->ether_addr_octet, ETH_ALEN);

    return result;
};

inline uint32_t parseIpv4(const std::string& ip)
{
    uint32_t out = 0;
    int      ret = inet_pton(AF_INET, ip.c_str(), &out);  // out is written in network byte order.

    if (ret != 1)
    {
        return 0;
    }
    return out;
}

inline void setLogContext(uint32_t deviceId, std::string hostName, [[maybe_unused]] uint64_t thread_id)
{
    hcl::LogManager::instance().setLogContext("host[" + hostName + "] device[" + std::to_string(deviceId) + "]");
    LOG_TRACE(HCL, "Set log context to device {} in host {}", deviceId, hostName);
}

/**
 * @brief This helper functions adds a value to a key inside a std::map
 *
 * @tparam T
 */
template<typename T>
struct map_init_helper
{
    T& data;
    map_init_helper(T& d) : data(d) {}
    map_init_helper& operator()(typename T::key_type const& key, typename T::mapped_type const& value)
    {
        data[key] = value;
        return *this;
    }
};

template<typename T>
map_init_helper<T> map_init(T& item)
{
    return map_init_helper<T>(item);
}

void* alloc_mem_to_be_mapped_to_device(size_t length,
                                       void*  addr   = nullptr,
                                       int    prot   = PROT_READ | PROT_WRITE,
                                       int    flags  = MAP_PRIVATE | MAP_ANONYMOUS,
                                       int    fd     = -1,
                                       off_t  offset = 0);

void* alloc_and_map_to_device(size_t    length,
                              uint64_t& deviceHandle,
                              int       deviceFd,
                              void*     addr   = nullptr,
                              int       prot   = PROT_READ | PROT_WRITE,
                              int       flags  = MAP_PRIVATE | MAP_ANONYMOUS,
                              int       fd     = -1,
                              off_t     offset = 0);

void free_mem_mapped_to_device(void* hostAddr, int length, uint64_t deviceHandle = 0, int fd = -1);

extern const char* HCL_VERSION_HEAD;

// Should be used only for initialization step
#define LOG_INFO_F(loggerName, msg, ...)                                                                               \
    {                                                                                                                  \
        unsigned log_level = hcl::LogManager::instance().get_log_level(hcl::LogManager::LogType::loggerName);          \
        hcl::LogManager::instance().set_log_level(hcl::LogManager::LogType::loggerName, 2);                            \
        HLLOG_TYPED(loggerName, 2, msg, ##__VA_ARGS__);                                                                \
        hcl::LogManager::instance().set_log_level(hcl::LogManager::LogType::loggerName, log_level);                    \
    }

inline void hclPrintVersionToLog()
{
    static bool bPrinted = false;

    if (!bPrinted)
    {
        bPrinted = true;

        LOG_INFO_F(HCL,
                   "Version:\t{}.{}.{}",
                   HL_DRIVER_MAJOR,
                   HL_DRIVER_MINOR,
                   HL_DRIVER_PATCHLEVEL);
    }
}
extern "C" {
__attribute__((__visibility__("default"))) void getHclVersion(char* pVersion, const unsigned len);
}

// A macro for not using parameters / members but not getting warned about it.
#define UNUSED(x) ((void)x)

void dumpStack(int s);
#define HCL_DUMP_STACK() dumpStack(0)

std::string getMemoryInfo();
#define HCL_MEM_STATS() LOG_ERR(HCL, "{}:{} - {}", __FILE__, __LINE__, getMemoryInfo())

/**
 * @brief Get current Process Memory Consumption In GB
 *
 * @return float - memory GBs
 */
float getProcMemConsInGB();

/**
 * @brief macro to send collective log from client to coordinator
 *        should be called from hccl API level only
 *        assuming hccl_ctx and comm are defined in calling code
 */
#define HCL_COLLECTIVE_LOG(op, count, dtype, reduce, peer, root)                                                       \
    {                                                                                                                  \
        if (unlikely((GCFG_HCL_COLLECTIVE_LOG.value() == true)))                                                       \
        {                                                                                                              \
            hccl_ctx.communicator(comm)->getCoordClient()->sendCollectiveLog(op, count, dtype, reduce, peer, root);    \
        }                                                                                                              \
    }

/**
 * @brief get string from a NicSet, used for logs
 *        format: "{x, y, z}"
 * @param nics - set of nics
 * @return std::string
 */
inline std::string nicsSetToString(const std::unordered_set<int>& nics)
{
    std::unordered_set<int>::const_iterator it;
    std::stringstream                       ss;
    ss << "{";
    unsigned i = 0;
    for (it = nics.begin(); it != nics.end(); it++)
    {
        ss << *it;
        if (i + 1 < nics.size()) ss << ", ";
        i++;
    }
    ss << "}";
    return ss.str();
}

/**
 * @brief check if working in Loopback mode
 *
 * @return true  - Loopback mode is set
 * @return false - Loopback mode is not set
 */
inline bool isLoopbackMode()
{
    if ((HclConfigType)GCFG_BOX_TYPE_ID.value() == HclConfigType::LOOPBACK)
    {
        return true;
    }
    return false;
}

/**
 * @brief check if type size is 2 bytes
 *
 * @return true  - type size is 2 bytes
 * @return false - type size is not 2 bytes
 */
#define FOR_I(N) for (unsigned i = 0; i < (N); i++)

inline bool isDataTypeTwoBytes(hcclDataType_t dataType)
{
    return dataTypeSizeInBytes(dataType) == 2;
}