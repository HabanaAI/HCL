#pragma once
#define HLLOG_ENABLE_LAZY_LOGGING
#include <hl_logger/hllog.hpp>
#include <chrono>

#ifndef unlikely
#define unlikely HLLOG_UNLIKELY
#endif
#ifndef likely
#define likely HLLOG_LIKELY
#endif

#ifndef HLLOG_DEFINE_OSTREAM_FORMATTER
#define HLLOG_DEFINE_OSTREAM_FORMATTER(full_type_name)
#endif

namespace hcl
{
class LogManager
{
public:
    enum class LogType : uint32_t
    {
        HCL_API,
        HCL,
        HCL_OFI,
        HCL_SUBMIT,
        HCL_SCAL,
        HCL_SIMB,
        HCL_SYNCOB,
        HCL_ECR,
        HCL_PROACT,
        HCL_COORD,
        HCL_IBV,
        HCL_TEST,
        FUNC_SCOPE,
        HCL_CG,
        HCL_FAILOVER,
        LOG_MAX  // Must be last
    };
#define HLLOG_ENUM_TYPE_NAME hcl::LogManager::LogType
    static LogManager& instance();

    ~LogManager();

    void create_logger(const LogType&     logType,
                       const std::string& fileName,
                       unsigned           logFileSize,
                       unsigned           logFileAmount,
                       const char*        separateLogFile = nullptr,
                       bool               sepLogPerThread = false);

    void set_log_level(const LogType& logType, unsigned log_level);

    static unsigned get_log_level(const LogManager::LogType logType) { return hl_logger::getLoggingLevel(logType); }

    static unsigned get_log_level_no_check(const LogManager::LogType logType)
    {
        return hl_logger::getLoggingLevel(logType);
    }

    void set_logger_sink(const LogType& logType, const std::string& pathname, unsigned lvl, size_t size, size_t amount);

    void log_wrapper(const LogManager::LogType& logType, const int logLevel, std::string&& s);
    void setLogContext(const std::string& logContext);

    void clearLogContext();

    static bool getLogsFolderPath(std::string& logsFolderPath);

    void flush();

    // in some scenarios periodic flush causes issues.
    // so it's required to disable periodic flush in such cases
    // e.i. synDestroy, death tests etc.
    void enablePeriodicFlush(bool enable = true);
    using LogSinks = hl_logger::SinksSPtr;
    // getLogSinks() & setLogSinks() are used for testing, to change the log file and later recover it
    LogSinks               setLogSinks(const LogManager::LogType& logType, LogSinks logSinks = LogSinks());
    LogSinks               getLogSinks(const LogManager::LogType& logType) const;
    void                   setLogSinks(const LogManager::LogType& logType, const std::string& newLogFileName);
    const std::string_view getLogTypeString(const LogType& logType) const;

private:
    LogManager();
};

// Class for scoped log objects. The object will log at the creation and destruction execution only.
class FuncScopeLog
{
public:
    FuncScopeLog(const std::string& function);

    ~FuncScopeLog();

private:
    const std::string m_function;
};

}  // namespace hcl

using TempLogContextSetter = hl_logger::ScopedLogContext;

#define SET_TEMP_LOG_CONTEXT(context) TempLogContextSetter __tempLogContextSetter(context);

template<unsigned LEVEL>
constexpr bool log_level_at_least(const hcl::LogManager::LogType logType)
{
    return hl_logger::logLevelAtLeast(logType, LEVEL);
}

template<unsigned LEVEL, hcl::LogManager::LogType logType>
constexpr bool log_level_at_least()
{
    static_assert(logType < hcl::LogManager::LogType::LOG_MAX, "logType is too large");
    return hl_logger::logLevelAtLeast(logType, LEVEL);
}

inline bool log_level_at_least(const hcl::LogManager::LogType logType, unsigned int level)
{
    return hcl::LogManager::get_log_level(logType) <= level;
}

#define LOG_LEVEL_AT_LEAST_TRACE(log_type)    HLLOG_LEVEL_AT_LEAST_TRACE(log_type)
#define LOG_LEVEL_AT_LEAST_DEBUG(log_type)    HLLOG_LEVEL_AT_LEAST_DEBUG(log_type)
#define LOG_LEVEL_AT_LEAST_INFO(log_type)     HLLOG_LEVEL_AT_LEAST_INFO(log_type)
#define LOG_LEVEL_AT_LEAST_WARN(log_type)     HLLOG_LEVEL_AT_LEAST_WARN(log_type)
#define LOG_LEVEL_AT_LEAST_ERR(log_type)      HLLOG_LEVEL_AT_LEAST_ERR(log_type)
#define LOG_LEVEL_AT_LEAST_CRITICAL(log_type) HLLOG_LEVEL_AT_LEAST_CRITICAL(log_type)

#define SEPARATOR_STR "+------------------------------------------------------------"

#define TITLE_STR(msg, ...)                                                                                            \
    "{}", fmt::format("{:=^120}", fmt::format((strlen(msg) == 0) ? "" : " " msg " ", ##__VA_ARGS__))

#define LOG_TRACE(log_type, msg, ...)    HLLOG_TRACE(log_type, msg, ##__VA_ARGS__);
#define LOG_DEBUG(log_type, msg, ...)    HLLOG_DEBUG(log_type, msg, ##__VA_ARGS__);
#define LOG_INFO(log_type, msg, ...)     HLLOG_INFO(log_type, msg, ##__VA_ARGS__);
#define LOG_WARN(log_type, msg, ...)     HLLOG_WARN(log_type, msg, ##__VA_ARGS__);
#define LOG_ERR(log_type, msg, ...)      HLLOG_ERR(log_type, msg, ##__VA_ARGS__);
#define LOG_CRITICAL(log_type, msg, ...) HLLOG_CRITICAL(log_type, msg, ##__VA_ARGS__);

#define LOG_TRACE_T    LOG_TRACE
#define LOG_DEBUG_T    LOG_DEBUG
#define LOG_INFO_T     LOG_INFO
#define LOG_WARN_T     LOG_WARN
#define LOG_ERR_T      LOG_ERR
#define LOG_CRITICAL_T LOG_CRITICAL

#define LOG_PERIODIC_BY_LEVEL(log_type, logLevel, period, maxNumLogsPerPeriod, msgFmt, ...)                            \
    do                                                                                                                 \
    {                                                                                                                  \
        static_assert(std::is_convertible_v<decltype(period), std::chrono::microseconds>,                              \
                      "period must be of std::chrono::duration type");                                                 \
        if (HLLOG_UNLIKELY(hl_logger::logLevelAtLeast(HLLOG_ENUM_TYPE_NAME::log_type, logLevel)))                      \
        {                                                                                                              \
            using time_point                             = std::chrono::time_point<std::chrono::steady_clock>;         \
            static time_point            epochStartPoint = std::chrono::steady_clock::now();                           \
            static std::atomic<uint64_t> msgCnt          = 0;                                                          \
            time_point                   curTimePoint    = std::chrono::steady_clock::now();                           \
            auto elapseTime = std::chrono::duration_cast<std::chrono::microseconds>(curTimePoint - epochStartPoint);   \
            if (msgCnt >= maxNumLogsPerPeriod)                                                                         \
            {                                                                                                          \
                if (elapseTime >= period)                                                                              \
                {                                                                                                      \
                    if (msgCnt > maxNumLogsPerPeriod)                                                                  \
                    {                                                                                                  \
                        HLLOG_TYPED(log_type,                                                                          \
                                    logLevel,                                                                          \
                                    msgFmt " : unpause. missed {} messages.",                                          \
                                    ##__VA_ARGS__,                                                                     \
                                    msgCnt - maxNumLogsPerPeriod - 1);                                                 \
                    }                                                                                                  \
                    else                                                                                               \
                    {                                                                                                  \
                        HLLOG_TYPED(log_type, logLevel, msgFmt, ##__VA_ARGS__);                                        \
                    }                                                                                                  \
                    msgCnt          = 0;                                                                               \
                    epochStartPoint = curTimePoint;                                                                    \
                }                                                                                                      \
                else                                                                                                   \
                {                                                                                                      \
                    if (msgCnt == maxNumLogsPerPeriod)                                                                 \
                    {                                                                                                  \
                        auto waitTime =                                                                                \
                            std::chrono::duration_cast<std::chrono::microseconds>(period - elapseTime).count();        \
                        HLLOG_TYPED(log_type,                                                                          \
                                    logLevel,                                                                          \
                                    msgFmt " : The message is generated too often. pause for {}ms.",                   \
                                    ##__VA_ARGS__,                                                                     \
                                    (waitTime + 500) / 1000);                                                          \
                    }                                                                                                  \
                    msgCnt++;                                                                                          \
                }                                                                                                      \
            }                                                                                                          \
            else                                                                                                       \
            {                                                                                                          \
                HLLOG_TYPED(log_type, logLevel, msgFmt, ##__VA_ARGS__);                                                \
                if (elapseTime >= period)                                                                              \
                {                                                                                                      \
                    epochStartPoint = curTimePoint;                                                                    \
                    msgCnt          = 0;                                                                               \
                }                                                                                                      \
                else                                                                                                   \
                {                                                                                                      \
                    msgCnt++;                                                                                          \
                }                                                                                                      \
            }                                                                                                          \
        }                                                                                                              \
    } while (false)

/**
 * LOG_PERIODIC
 * @brief log frequent message - maximum maxNumLogsPerPeriod times per period
 *        after maxNumLogsPerPeriod pauses printing until the end of period
 * @param log_type logger type
 * @param period   period std::chrono::duration (e.g.milliseconds)
 * @param maxNumLogsPerPeriod max number of messages per period
 * @param msgFmt   message format
 */
#define LOG_PERIODIC_TRACE(log_type, period, maxNumLogsPerPeriod, msgFmt, ...)                                         \
    LOG_PERIODIC_BY_LEVEL(log_type, HLLOG_LEVEL_TRACE, period, maxNumLogsPerPeriod, msgFmt, ##__VA_ARGS__);
#define LOG_PERIODIC_DEBUG(log_type, period, maxNumLogsPerPeriod, msgFmt, ...)                                         \
    LOG_PERIODIC_BY_LEVEL(log_type, HLLOG_LEVEL_DEBUG, period, maxNumLogsPerPeriod, msgFmt, ##__VA_ARGS__);
#define LOG_PERIODIC_INFO(log_type, period, maxNumLogsPerPeriod, msgFmt, ...)                                          \
    LOG_PERIODIC_BY_LEVEL(log_type, HLLOG_LEVEL_INFO, period, maxNumLogsPerPeriod, msgFmt, ##__VA_ARGS__);
#define LOG_PERIODIC_WARN(log_type, period, maxNumLogsPerPeriod, msgFmt, ...)                                          \
    LOG_PERIODIC_BY_LEVEL(log_type, HLLOG_LEVEL_WARN, period, maxNumLogsPerPeriod, msgFmt, ##__VA_ARGS__);
#define LOG_PERIODIC_ERR(log_type, period, maxNumLogsPerPeriod, msgFmt, ...)                                           \
    LOG_PERIODIC_BY_LEVEL(log_type, HLLOG_LEVEL_ERROR, period, maxNumLogsPerPeriod, msgFmt, ##__VA_ARGS__);

#define STATIC_LOG_TRACE    LOG_TRACE
#define STATIC_LOG_DEBUG    LOG_DEBUG
#define STATIC_LOG_INFO     LOG_INFO
#define STATIC_LOG_WARN     LOG_WARN
#define STATIC_LOG_ERR      LOG_ERR
#define STATIC_LOG_CRITICAL LOG_CRITICAL

#define LOG_FUNC_SCOPE() hcl::FuncScopeLog log(__FUNCTION__)

#define TO64(x)  ((uint64_t)x)
#define TO64P(x) ((void*)x)
