#pragma once
#include "../hllog_core.hpp"
#include <optional>

// internal api
namespace hl_logger::internal{
inline namespace v1_0{

// remove after promotion
// most libs are built with fPIC and TLS access causes _tls_get_addr calls
// so we use getThreadId() instead and build hl_logger to disable _tls_get_addr
extern thread_local uint64_t s_threadId;
// now use getThreadId for getting thread id
HLLOG_API uint64_t getThreadId();

extern uint32_t              s_internalLogLevel;

HLLOG_API void logInternal(int logLevel, std::string_view msg);

#define HLLOG_INTERNAL_LOG(logLevel, fmtMsg, ...)                                                                      \
do {                                                                                                                   \
    if (hl_logger::internal::s_internalLogLevel <= logLevel && logLevel < HLLOG_LEVEL_OFF) {                           \
        fmt::memory_buffer buf;                                                                                        \
        fmt::format_to(std::back_inserter(buf), FMT_COMPILE("{}: " fmtMsg) , __FUNCTION__, ##__VA_ARGS__);             \
        hl_logger::internal::logInternal(logLevel, std::string_view(buf.data(), buf.size()));                          \
    }                                                                                                                  \
}while(false)

#define HLLOG_INTERNAL_TRACE(fmtMsg, ...)    HLLOG_INTERNAL_LOG(HLLOG_LEVEL_TRACE, fmtMsg, ##__VA_ARGS__)
#define HLLOG_INTERNAL_DEBUG(fmtMsg, ...)    HLLOG_INTERNAL_LOG(HLLOG_LEVEL_DEBUG, fmtMsg, ##__VA_ARGS__)
#define HLLOG_INTERNAL_INFO(fmtMsg, ...)     HLLOG_INTERNAL_LOG(HLLOG_LEVEL_INFO,  fmtMsg, ##__VA_ARGS__)
#define HLLOG_INTERNAL_WARN(fmtMsg, ...)     HLLOG_INTERNAL_LOG(HLLOG_LEVEL_WARN,  fmtMsg, ##__VA_ARGS__)
#define HLLOG_INTERNAL_ERR(fmtMsg, ...)      HLLOG_INTERNAL_LOG(HLLOG_LEVEL_ERROR, fmtMsg, ##__VA_ARGS__)
#define HLLOG_INTERNAL_CRITICAL(fmtMsg, ...) HLLOG_INTERNAL_LOG(HLLOG_LEVEL_CRITICAL, fmtMsg, ##__VA_ARGS__)

using TimePoint = std::chrono::system_clock::time_point;

HLLOG_API TimePoint tscToRt(uint64_t tsc);
}

inline namespace v1_1 {
struct FormattedLazyLogItem
{
    TimePoint   timePoint{};
    int         logLevel{HLLOG_LEVEL_OFF};
    uint64_t    tid{};
    string_wrapper msg{};
};
using FormattedLazyLogItemOptional = std::optional<FormattedLazyLogItem>;

struct LazyLogInfo
{
    std::string_view loggerName;
    std::function<FormattedLazyLogItemOptional ()> getNextLogItemFunc;
};
using LazyLogInfoVector = std::vector<LazyLogInfo>;
using LazyLogsHandler   = std::function<LazyLogInfoVector ()>;

HLLOG_API ResourceGuard registerLazyLogsHandler(LazyLogsHandler lazyLogsHandler, std::string_view moduleName);
}

inline namespace v1_0 {
[[deprecated("use an overload with moduleName")]]HLLOG_API ResourceGuard registerLazyLogsHandler(LazyLogsHandler lazyLogsHandler);

struct IFormatter
{
    virtual ~IFormatter() = default;
    virtual std::string format() const = 0;
};

template <class ... TArgs>
struct Formatter : IFormatter
{
    using ArgsAsTuple = std::tuple<TArgs...>;

    template<class ... Args>
    Formatter(Args && ... args)
    : _argsAsTuple(std::forward<Args>(args)...)
    {

    }
    Formatter(void * argsAsTuplePtr)
    : _argsAsTuple(std::move(*reinterpret_cast<ArgsAsTuple*>(argsAsTuplePtr)))
    {

    }
    std::string format() const
    {
        return std::apply([](auto const & ...args){ return fmt::format(args...); }, _argsAsTuple);
    }
private:
    ArgsAsTuple  _argsAsTuple;
};

using CreateFormatterFunc = IFormatter * (void * buffer, unsigned bufferSize, void * argsAsTupleVoidPtr);

HLLOG_API void logLazy(LoggerSPtr const& logger, int logLevel, CreateFormatterFunc * createFormatterFunc, void * argsAsTupleVoidPtr);

using AddToRecentLogsQueueFunc = void (void* recentLogsQueueVoidPtr,
                                       uint8_t logLevel,
                                       CreateFormatterFunc * createFormatterFunc,
                                       void * argsAsTupleVoidPtr);
HLLOG_API void setLoggerRecentLogsQueue(LoggerSPtr const& logger, AddToRecentLogsQueueFunc * addToRecentLogsQueueFunc, void * recentLogsQueueVoidPtr);

using LoggingLevelByMaskHandler = std::function<void(std::string_view mask, int newLevel)>;
HLLOG_API ResourceGuard registerLoggingLevelByMaskHandler(LoggingLevelByMaskHandler loggerLevelByMaskHandler, std::string_view moduleName);

using GetLoggingLevelByNameHandler = std::function<int(std::string_view mask)>;
HLLOG_API ResourceGuard registerGetLoggingLevelByNameHandler(GetLoggingLevelByNameHandler loggerLevelByNameHandler, std::string_view moduleName);

[[deprecated("use an overload with moduleName")]]HLLOG_API ResourceGuard registerLoggingLevelByMaskHandler(LoggingLevelByMaskHandler loggerLevelByMaskHandler);
[[deprecated("use an overload with moduleName")]]HLLOG_API ResourceGuard registerGetLoggingLevelByNameHandler(GetLoggingLevelByNameHandler loggerLevelByNameHandler);

} // vXX
}// hl_logger::internal namespace
