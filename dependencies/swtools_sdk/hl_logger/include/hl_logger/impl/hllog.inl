#pragma once
#include "recent_elements_queue.hpp"
#include <chrono>
#include <atomic>

#include "hllog_internal_api.hpp"

// with rdtsc on some systems time is not consistent
// disable rdtsc until a fix
#ifndef HLLOG_USE_STD_TIMESTAMP
#define HLLOG_USE_STD_TIMESTAMP
#endif

#ifndef HLLOG_USE_STD_TIMESTAMP
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#endif //HLLOG_USE_STD_TIMESTAMP

HLLOG_BEGIN_NAMESPACE
// fix type fo temporal objects for lazy logging e.g. const char* and join
// for other types there is no need to fix. so we use a type without a change
template<class T>
T lazyLogFixType(T const &);

// Required for temporal char-array (string.c_str()) for storing content into a string,
// instead of holding it as a pointer
// This is required as the string content might change during run, and effect the stored info
std::string lazyLogFixType(const char*);
template <unsigned N>
std::string lazyLogFixType(const char[N]);

template<class T>
T lazyLogFixType(std::atomic<T> const &);

template<class TItem>
struct JoinHolder
{
    using Iterator = typename std::vector<TItem>::iterator;

    template<class It, class Sentinel>
    JoinHolder(fmt::join_view<It, Sentinel> join)
            : values(join.begin, join.end)
            , sep(join.sep)
    {
    }
    JoinHolder(JoinHolder&&) = default;
    JoinHolder(JoinHolder const&) = default;
    std::vector<TItem> values;
    fmt::basic_string_view<char>     sep;
};

template<class It, class Sentinel>
auto lazyLogFixType(fmt::join_view<It, Sentinel> const &) -> JoinHolder<std::remove_cv_t<std::remove_reference_t<decltype(*std::declval<It>())>>>;

struct static_string
{
    template <size_t N>
    constexpr static_string(const char (&str)[N]) : m_view(str){}
    constexpr static_string(static_string &&) = default;
    constexpr static_string(static_string const &) = default;
    constexpr std::string_view as_string_view() const { return m_view; }
private:
    std::string_view m_view;
};

HLLOG_END_NAMESPACE

template<class TItem>
struct fmt::formatter<hl_logger::JoinHolder<TItem>> : fmt::formatter<fmt::join_view<typename hl_logger::JoinHolder<TItem>::Iterator, typename hl_logger::JoinHolder<TItem>::Iterator>, char>
{
    template<typename FormatContext>
    auto format(hl_logger::JoinHolder<TItem> const & joinHolder, FormatContext& ctx) const
    {
        auto & ncHolder = const_cast<hl_logger::JoinHolder<TItem>&>(joinHolder);
        using It = typename hl_logger::JoinHolder<TItem>::Iterator;
        return fmt::formatter<fmt::join_view<It, It>, char>::format(fmt::join_view<It, It>{ncHolder.values.begin(), ncHolder.values.end(), ncHolder.sep}, ctx);
    }
};

template <>
struct fmt::formatter<hl_logger::static_string> : fmt::formatter<std::string_view, char>
{
    template<typename FormatContext>
    auto format(hl_logger::static_string const & str, FormatContext& ctx) const
    {
        return fmt::formatter<std::string_view, char>::format(str.as_string_view(), ctx);
    }
};

HLLOG_BEGIN_NAMESPACE
#ifndef HLLOG_USE_STD_TIMESTAMP
        using LazyLogItemTimePoint = uint64_t;
#else
        using LazyLogItemTimePoint = std::chrono::system_clock::time_point;
#endif

template <unsigned BufferSize>
class LazyLogItem
{
public:
    template<class TFmtMsg, class ... TArgs>
    LazyLogItem(int logLevel, TFmtMsg fmtMsg, TArgs && ... args)
    : m_logLevel(logLevel)
    , m_tid(internal::s_threadId)
#ifndef HLLOG_USE_STD_TIMESTAMP
    , m_time(__rdtsc())
#else
    , m_time(std::chrono::system_clock::now())
#endif
    {
        using TFormatter = internal::Formatter<TFmtMsg, decltype(lazyLogFixType(std::declval<TArgs>()))...>;

        if constexpr (sizeof(TFormatter) <= sizeof(m_tupleBuffer))
        {
            // Constructing without heap allocation
            m_formatter = reinterpret_cast<internal::IFormatter*>(m_tupleBuffer);
            new (m_tupleBuffer) TFormatter(std::move(fmtMsg), std::forward<TArgs>(args)...);
        }
        else
        {
            // large objects cause heap allocation
            m_formatter = new TFormatter(std::move(fmtMsg), std::forward<TArgs>(args)...);
        }
    }
    // for untyped logging we don't have direct access to the arguments and must use type erasure
    // 1. caller must erase args type (provides LazyLogUntypedFuncs)
    // 2. logger must erase queue type (provides addToRecentElemetsQueueFunc)
    LazyLogItem(int logLevel, internal::CreateFormatterFunc * createFormatter, void * argsAsTupleVoidPtr)
    : m_logLevel(logLevel)
    , m_tid(internal::s_threadId)
#ifndef HLLOG_USE_STD_TIMESTAMP
    , m_time(__rdtsc())
#else
    , m_time(std::chrono::system_clock::now())
#endif
    {
        m_formatter = createFormatter(m_tupleBuffer, sizeof(m_tupleBuffer), argsAsTupleVoidPtr);
    }

    LazyLogItem() = default;
    LazyLogItem(LazyLogItem const&) = delete;
    LazyLogItem(LazyLogItem&& other) = delete;
    ~LazyLogItem()
    {
        if (reinterpret_cast<void*>(m_formatter) != m_tupleBuffer)
        {
            delete m_formatter;
        }
        else
        {
            m_formatter->~IFormatter();
        }
    }

    // Executing Lambda functionality
    internal::FormattedLazyLogItem getAsFormattedLazyItem() const
    {
        internal::FormattedLazyLogItem formattedLazyLogItem{
#ifndef HLLOG_USE_STD_TIMESTAMP
            internal::tscToRt(m_time),
#else
            m_time,
#endif
            m_logLevel,
            m_tid};
        if (m_formatter)
        {
            formattedLazyLogItem.msg = m_formatter->format();
        }
        return formattedLazyLogItem;
    }
    explicit operator bool() const { return (m_formatter != nullptr); }

private:
    uint8_t   m_logLevel   = 0;

    uint64_t             m_tid;
    LazyLogItemTimePoint m_time;
    uint8_t   m_tupleBuffer[BufferSize] = {};  // For saving from allocation calls
    internal::IFormatter * m_formatter  = nullptr;
};

using RecentLogsQueue = containers::ConcurrentRecentElementsQueue<LazyLogItem<64>>;

struct LogLevelInfo{
    uint8_t logLevel;
    uint8_t lazyLogLevel;
};

template<class TLoggerEnum>
struct ModuleLoggerData
{
    static constexpr unsigned                      nbEnumItems = hl_logger::getNbLoggers<TLoggerEnum>();
    static std::array<LogLevelInfo, nbEnumItems>   levels;
    static bool                                    scanLoggerNamesMode;
    using RecentLogQueueUPtr = std::unique_ptr<RecentLogsQueue>;
    struct LoggerData{
        std::atomic<bool>     initialized;
        hl_logger::LoggerSPtr logger;
    };
    std::array<LoggerData, nbEnumItems>            loggers;
    std::array<RecentLogQueueUPtr, nbEnumItems>    lazyLoggerQueues;
    std::array<std::function<void()>, nbEnumItems> loggerOnDemandCreators;
    std::array<bool, nbEnumItems>                  registered;
    std::array<uint8_t, nbEnumItems>               consoleLevels;
    unsigned                                       maxLoggerNameLen = 0;
    hl_logger::ResourceGuard                       signalHandlerResourceGuard;
    hl_logger::ResourceGuard                       flushHandlerResourceGuard;
    hl_logger::ResourceGuard                       lazyLogsHandlerResourceGuard;
    hl_logger::ResourceGuard                       loggersLevelByMaskHandlerResourceGuard;
    hl_logger::ResourceGuard                       getLoggersLevelByNameHandlerResourceGuard;
    std::unordered_map<std::string, uint32_t>      maxLoggerNameLenPerFile;
    std::array<uint32_t, nbEnumItems>              lazyQueueSizes;
    std::mutex                                     userLogsQueueCreateMtx;
    std::string_view                               moduleName;
    ModuleLoggerData(std::string_view moduleName);
    ~ModuleLoggerData();
};

template<class TLoggerEnum>
extern ModuleLoggerData<TLoggerEnum> moduleLoggerData;

template<typename TFmtMsg, typename... Args>
inline void log(LoggerSPtr const & logger, int logLevel, bool forcePrint, bool forcePrintFileLine, std::string_view file, int line, TFmtMsg fmtMsg, Args&&... args)
try
{
    fmt::memory_buffer buf;
    fmt::format_to(std::back_inserter(buf), fmtMsg, std::forward<Args>(args)...);
    hl_logger::log(logger, logLevel, forcePrint, std::string_view(buf.data(), buf.size()), file, line, forcePrintFileLine);
}
catch(std::exception const & e)
{
    hl_logger::log(logger, HLLOG_LEVEL_ERROR, "failed to format message");
    hl_logger::log(logger, HLLOG_LEVEL_ERROR, e.what());
    hl_logger::logStackTrace(logger, HLLOG_LEVEL_ERROR);
}
catch(...)
{
    hl_logger::log(logger, HLLOG_LEVEL_ERROR, "failed to format message");
    hl_logger::logStackTrace(logger, HLLOG_LEVEL_ERROR);
}

template<class TLoggerEnum, typename TFmtMsg, typename... Args>
inline void log(TLoggerEnum loggerEnumItem, int logLevel, bool forcePrint, bool forcePrintFileLine, std::string_view file, int line, TFmtMsg fmtMsg, Args&&... args)
{
    auto logger = getLogger(loggerEnumItem);
    log(logger, logLevel, forcePrint, forcePrintFileLine, file, line, fmtMsg, std::forward<Args>(args)...);
}

template<class TLoggerEnum, typename TFmtMsg, typename... Args>
inline void log(TLoggerEnum loggerEnumItem, int logLevel, bool forcePrintFileLine, std::string_view file, int line, TFmtMsg fmtMsg, Args&&... args)
{
    log(loggerEnumItem, logLevel, false, forcePrintFileLine, file, line, fmtMsg, std::forward<Args>(args)...);
}

template<class TLoggerEnum, typename TFmtMsg, typename... TArgs>
inline void logLazy(TLoggerEnum loggerEnumItem, int logLevel, TFmtMsg fmtMsg, TArgs&&... args)
{
    moduleLoggerData<TLoggerEnum>.lazyLoggerQueues[(uint32_t)loggerEnumItem]->emplace(logLevel, std::move(fmtMsg), std::forward<TArgs>(args)...);
}

template<typename TFmtMsg, typename... TArgs>
inline void logLazy(LoggerSPtr const& logger, int logLevel, TFmtMsg fmtMsg, TArgs&&... args)
{
    using TFormatter = internal::Formatter<TFmtMsg, std::remove_reference_t<TArgs>...>;
    typename TFormatter::ArgsAsTuple argsAsTuple(std::move(fmtMsg), std::forward<TArgs>(args)...);

    hl_logger::internal::logLazy(logger, logLevel, [](void * buffer, unsigned bufferSize, void * argsAsTupleVoidPtr) ->internal::IFormatter*{
            if (sizeof(TFormatter) <= bufferSize)
            {
                return new (buffer) TFormatter(argsAsTupleVoidPtr);
            }
            return new TFormatter(argsAsTupleVoidPtr);
        }, &argsAsTuple);
}
template <class TLoggerEnum>
void setLoggerRecentLogsQueue(TLoggerEnum loggerEnumItem)
{
    auto logger = getLogger(loggerEnumItem);
    unsigned loggerIndex = static_cast<unsigned>(loggerEnumItem);
    auto addToRecentLogsQueueFunc = [](void* recentLogsQueueVoidPtr,
                                       uint8_t logLevel,
                                       internal::CreateFormatterFunc * createFormatterFunc,
                                       void * argsAsTupleVoidPtr){

        RecentLogsQueue & recentLogsQueue = *reinterpret_cast<RecentLogsQueue*>(recentLogsQueueVoidPtr);
        recentLogsQueue.emplace(logLevel, createFormatterFunc, argsAsTupleVoidPtr);
    };
    hl_logger::internal::setLoggerRecentLogsQueue(logger,
                                                  addToRecentLogsQueueFunc,
                                                  moduleLoggerData<TLoggerEnum>.lazyLoggerQueues[loggerIndex].get());
}

// default empty implementation of createModuleLoggersOnDemand
// in case there are no on demand loggers
template <class TLoggerEnum>
inline void createModuleLoggersOnDemand(TLoggerEnum) {}
template <class TLoggerEnum>
inline void onModuleLoggersBeforeDestroy(TLoggerEnum) {}
template <class TLoggerEnum>
void onModuleLoggersCrashSignal(TLoggerEnum, int signal, const char * signalStr){}
template <class TLoggerEnum>
void onModuleLoggersCrashSignal(TLoggerEnum, int signal, const char * signalStr, bool isSevere){}
template<class TLoggerEnum>
inline ModuleLoggerData<TLoggerEnum>::ModuleLoggerData(std::string_view moduleName)
: moduleName(moduleName)
{
    HLLOG_INTERNAL_INFO("initialization of module: {}. logging dir: {} logging dir from env: {}", moduleName, hl_logger::getLogsFolderPath(), hl_logger::getLogsFolderPathFromEnv());
    const LogLevelInfo levelsOff{HLLOG_LEVEL_OFF, HLLOG_LEVEL_OFF};
    levels.fill(levelsOff);
    registered.fill(false);
    consoleLevels.fill(HLLOG_LEVEL_INVALID);
    lazyQueueSizes.fill(HLLOG_DEFAULT_LAZY_QUEUE_SIZE);
    std::string_view ::size_type maxLoggerNameLen = 0;
    for (unsigned i = 0 ; i < hl_logger::getNbLoggers<TLoggerEnum>(); ++i)
    {
        maxLoggerNameLen = std::max(maxLoggerNameLen, hl_logger::getLoggerEnumItemName(TLoggerEnum(i)).size());
    }
    moduleLoggerData<TLoggerEnum>.maxLoggerNameLen = maxLoggerNameLen;

    scanLoggerNamesMode = true;
    createModuleLoggers(TLoggerEnum());
    createModuleLoggersOnDemand(TLoggerEnum());
    scanLoggerNamesMode = false;

    createModuleLoggers(TLoggerEnum());
    createModuleLoggersOnDemand(TLoggerEnum());

    for (unsigned i = 0; i < loggers.size(); ++i)
    {
        if (loggers[i].logger && lazyLoggerQueues[i])
        {
            setLoggerRecentLogsQueue(TLoggerEnum(i));
        }
    }

    signalHandlerResourceGuard = registerSignalHandler([](int signum, const char* sigstr, bool isSevere){
        onModuleLoggersCrashSignal(TLoggerEnum(), signum , sigstr);
        onModuleLoggersCrashSignal(TLoggerEnum(), signum , sigstr, isSevere);
        flushAll<TLoggerEnum>();
    }, moduleName);

    flushHandlerResourceGuard = registerFlushHandler([](){
        flushAll<TLoggerEnum>();
    }, moduleName);

    lazyLogsHandlerResourceGuard = internal::registerLazyLogsHandler([this](){
        internal::LazyLogInfoVector lazyLogInfos;
        for (unsigned i = 0 ; i < nbEnumItems; ++i)
        {
            if (lazyLoggerQueues[i] && !lazyLoggerQueues[i]->empty())
            {
                internal::LazyLogInfo loginfo;
                loginfo.loggerName = getLoggerEnumItemName(TLoggerEnum(i));
                auto iteratorToProcess = lazyLoggerQueues[i]->getIteratorToProcessAndClean();
                loginfo.getNextLogItemFunc = [it = std::move(iteratorToProcess)]() mutable {
                    internal::FormattedLazyLogItemOptional lazyLogItem;
                    if (it)
                    {
                        internal::FormattedLazyLogItem formattedItem = [&]() {
                            auto v = *it; // get the value and lock it for MT protection
                            return v ? v->getAsFormattedLazyItem() : internal::FormattedLazyLogItem();
                        }();
                        // if the value is valid (not empty) - process it
                        if (formattedItem.logLevel != HLLOG_LEVEL_OFF)
                        {
                            lazyLogItem.emplace(std::move(formattedItem));
                        }
                        ++it;
                    }
                    return lazyLogItem;
                };
                lazyLogInfos.push_back(std::move(loginfo));
            }
        }
        return lazyLogInfos;
    }, moduleName);

    loggersLevelByMaskHandlerResourceGuard = internal::registerLoggingLevelByMaskHandler([](std::string_view mask, int newLevel){
        constexpr std::string_view logLevelAll = "LOG_LEVEL_ALL";
        constexpr std::string_view logLevelAllPrefix = "LOG_LEVEL_ALL_";
        constexpr std::string_view logLevelStart = "LOG_LEVEL_";
        const bool forAll = mask == logLevelAll;

        if (mask.find(logLevelStart) != 0) return;
        bool isPrefix = mask.find(logLevelAllPrefix) == 0;
        std::string_view prefix = isPrefix ? mask.substr(logLevelAllPrefix.size()) : "";
        std::string_view fullMatch = !isPrefix ? mask.substr(logLevelStart.size()) : prefix;
        forEachLoggerEnumItem<TLoggerEnum>([newLevel, forAll, prefix, fullMatch](auto loggerItem){
            bool setLevel = forAll;
            if (!setLevel)
            {
                auto itemName = getLoggerEnumItemName(loggerItem);
                setLevel = itemName == fullMatch || (!prefix.empty() && itemName.find(prefix) == 0);
            }
            if (setLevel)
            {
                setLoggingLevel(loggerItem, newLevel);
            }
        });
    }, moduleName);

    getLoggersLevelByNameHandlerResourceGuard = internal::registerGetLoggingLevelByNameHandler([](std::string_view loggerName) -> int {
        int result = HLLOG_LEVEL_INVALID;

        forEachLoggerEnumItem<TLoggerEnum>([loggerName, &result](auto loggerItem) {
            if (result == HLLOG_LEVEL_INVALID)
            {
                auto itemName = getLoggerEnumItemName(loggerItem);
                if (itemName == loggerName)
                {
                    result = getLoggingLevel(loggerItem);
                }
            }
        });

        return result;
    }, moduleName);
}

template<class TLoggerEnum>
inline ModuleLoggerData<TLoggerEnum>::~ModuleLoggerData()
{
    HLLOG_INTERNAL_INFO("destruction of module: {}", moduleName);
    onModuleLoggersBeforeDestroy(TLoggerEnum());
    // disable all the logs, preventing access after dtor
    const LogLevelInfo levelsOff{HLLOG_LEVEL_OFF, HLLOG_LEVEL_OFF};
    levels.fill(levelsOff);
    flushAll<TLoggerEnum>();
    for (unsigned i =0; i < nbEnumItems; ++i)
    {
        drop(TLoggerEnum(i));
    }
}

template <class TLoggerEnum>
inline void initLazyLoggerRecentLogsQueue(TLoggerEnum loggerEnumItem)
{
    // user lazy log
    auto loggerIdx = unsigned(loggerEnumItem);
    auto & lazyLoggerQueue = moduleLoggerData<TLoggerEnum>.lazyLoggerQueues[loggerIdx];
    if (lazyLoggerQueue == nullptr)
    {
        std::lock_guard lock(moduleLoggerData<TLoggerEnum>.userLogsQueueCreateMtx);
        if (lazyLoggerQueue == nullptr)
        {
            lazyLoggerQueue = std::make_unique<RecentLogsQueue>(moduleLoggerData<TLoggerEnum>.lazyQueueSizes[loggerIdx]);
        }
    }
}

template <class TLoggerEnum>
inline void updateLazyLoggerRecentLogsQueue(TLoggerEnum loggerEnumItem, hl_logger::LoggerCreateParams const & params)
{
    if (params.defaultLazyLoggingLevel < HLLOG_LEVEL_OFF && params.forceDefaultLazyLoggingLevel)
    {
        initLazyLoggerRecentLogsQueue(loggerEnumItem);
    }
    else if (hl_logger::getDefaultLazyLoggingLevel(hl_logger::getLoggerEnumItemName(loggerEnumItem), params.defaultLazyLoggingLevel) < HLLOG_LEVEL_OFF)
    {
        initLazyLoggerRecentLogsQueue(loggerEnumItem);
    }
}

template <class TLoggerEnum>
inline void createLogger(TLoggerEnum loggerEnumItem, hl_logger::LoggerCreateParams const & params_)
{
    auto loggerIdx = unsigned(loggerEnumItem);
    if (ModuleLoggerData<TLoggerEnum>::scanLoggerNamesMode)
    {
        uint32_t & maxLen = moduleLoggerData<TLoggerEnum>.maxLoggerNameLenPerFile[params_.logFileName];
        maxLen = std::max(maxLen, (uint32_t)getLoggerEnumItemName(loggerEnumItem).size());
        moduleLoggerData<TLoggerEnum>.lazyQueueSizes[loggerIdx] = getLazyQueueSize(getLoggerEnumItemName(loggerEnumItem), params_.defaultLazyQueueSize);
        return;
    }
    hl_logger::LoggerCreateParams params = params_;
    if (params_.loggerNameLength == 0)
    {
        auto it = moduleLoggerData<TLoggerEnum>.maxLoggerNameLenPerFile.find(params_.logFileName);
        if (it != moduleLoggerData<TLoggerEnum>.maxLoggerNameLenPerFile.end())
        {
            params.loggerNameLength = it->second;
        }
        if (params.loggerNameLength == 0)
        {
            params.loggerNameLength = moduleLoggerData<TLoggerEnum>.maxLoggerNameLen;
        }
    }
    if (params.registerLogger)
    {
        moduleLoggerData<TLoggerEnum>.registered[loggerIdx] = true;
    }
    hl_logger::LoggerSPtr newLogger = hl_logger::createLogger(hl_logger::getLoggerEnumItemName(loggerEnumItem), params);
    moduleLoggerData<TLoggerEnum>.loggers[loggerIdx].logger = newLogger;
    moduleLoggerData<TLoggerEnum>.loggers[loggerIdx].initialized.store(true, std::memory_order_release);
    if (params.defaultLoggingLevel != HLLOG_LEVEL_INVALID)
    {
        moduleLoggerData<TLoggerEnum>.levels[loggerIdx].logLevel = hl_logger::getLoggingLevel(newLogger);
    }
    else
    {
        hl_logger::setLoggingLevel(newLogger, moduleLoggerData<TLoggerEnum>.levels[loggerIdx].logLevel);
    }
    if (params.defaultConsoleLoggingLevel != HLLOG_LEVEL_INVALID)
    {
        moduleLoggerData<TLoggerEnum>.consoleLevels[loggerIdx] = hl_logger::getConsoleLoggingLevel(newLogger);
    }
    else
    {
        hl_logger::setConsoleLoggingLevel(newLogger, moduleLoggerData<TLoggerEnum>.consoleLevels[loggerIdx]);
    }
    if (params.defaultLazyLoggingLevel != HLLOG_LEVEL_INVALID)
    {
        moduleLoggerData<TLoggerEnum>.levels[loggerIdx].lazyLogLevel = hl_logger::getLazyLoggingLevel(newLogger);
    }
    else
    {
        hl_logger::setLazyLoggingLevel(newLogger, moduleLoggerData<TLoggerEnum>.levels[loggerIdx].lazyLogLevel);
    }
    updateLazyLoggerRecentLogsQueue(loggerEnumItem, params_);
}

template <class TLoggerEnum>
inline void createLoggerOnDemand(TLoggerEnum loggerEnumItem, hl_logger::LoggerCreateParams const & params_)
{
    auto loggerIdx = unsigned(loggerEnumItem);
    if (ModuleLoggerData<TLoggerEnum>::scanLoggerNamesMode)
    {
        auto & maxLen = moduleLoggerData<TLoggerEnum>.maxLoggerNameLenPerFile[params_.logFileName];
        maxLen = std::max(maxLen, (uint32_t)getLoggerEnumItemName(loggerEnumItem).size());
        moduleLoggerData<TLoggerEnum>.lazyQueueSizes[loggerIdx] = getLazyQueueSize(getLoggerEnumItemName(loggerEnumItem), params_.defaultLazyQueueSize);
        return;
    }
    hl_logger::LoggerCreateParams params = params_;

    const int defaultLogLevel = hl_logger::getDefaultLoggingLevel(hl_logger::getLoggerEnumItemName(loggerEnumItem), params.defaultLoggingLevel);
    const int logLevel = params.forceDefaultLoggingLevel ? params.defaultLoggingLevel : defaultLogLevel;
    moduleLoggerData<TLoggerEnum>.levels[loggerIdx].logLevel = logLevel;

    const int defaultConsoleLogLevel = hl_logger::getDefaultConsoleLoggingLevel(hl_logger::getLoggerEnumItemName(loggerEnumItem), params.defaultConsoleLoggingLevel);
    const int consoleLogLevel = params.forceDefaultConsoleLoggingLevel ? params.defaultConsoleLoggingLevel : defaultConsoleLogLevel;
    moduleLoggerData<TLoggerEnum>.consoleLevels[loggerIdx] = consoleLogLevel;

    const int defaultLazyLogLevel = hl_logger::getDefaultLazyLoggingLevel(hl_logger::getLoggerEnumItemName(loggerEnumItem), params.defaultLazyLoggingLevel);
    const int lazyLogLevel = params.forceDefaultLazyLoggingLevel ? params.defaultLazyLoggingLevel : defaultLazyLogLevel;
    moduleLoggerData<TLoggerEnum>.levels[loggerIdx].lazyLogLevel  = lazyLogLevel;

    params.defaultLoggingLevel = HLLOG_LEVEL_INVALID;
    params.defaultConsoleLoggingLevel = HLLOG_LEVEL_INVALID;
    params.defaultLazyLoggingLevel = HLLOG_LEVEL_INVALID;
    updateLazyLoggerRecentLogsQueue(loggerEnumItem, params_);
    HLLOG_INTERNAL_INFO("loggerName: {} defaultLogLevel: {} defaultLazyLoggingLevel: {}",
                        getLoggerEnumItemName(loggerEnumItem), params_.defaultLoggingLevel, params_.defaultLazyLoggingLevel);
    moduleLoggerData<TLoggerEnum>.loggerOnDemandCreators[loggerIdx] = [=](){
        createLogger(loggerEnumItem, params);
        setLoggerRecentLogsQueue(loggerEnumItem);
        auto logger = moduleLoggerData<TLoggerEnum>.loggers[loggerIdx].logger;
        setLazyLoggingLevel(logger, moduleLoggerData<TLoggerEnum>.levels[loggerIdx].lazyLogLevel);
    };
}
template <class TLoggerEnum>
inline void createLoggersOnDemand(std::initializer_list<TLoggerEnum> const& loggerEnumItems,
                                  hl_logger::LoggerCreateParams const &     params)
{
    for (TLoggerEnum enumItem :  loggerEnumItems)
    {
        createLoggerOnDemand(enumItem, params);
    }
}

template <class TLoggerEnum>
inline void createLoggers(std::initializer_list<TLoggerEnum> const & loggerEnumItems, hl_logger::LoggerCreateParams const & params)
{
    for (auto const & loggerEnumItem : loggerEnumItems) {
        createLogger(loggerEnumItem, params);
    }
}

template <class TLoggerEnum>
void setLoggingLevel(TLoggerEnum loggerEnumItem, int newLevel)
{
    auto loggerIdx = unsigned(loggerEnumItem);
    moduleLoggerData<TLoggerEnum>.levels[loggerIdx].logLevel = newLevel;
    if (isLoggerInstantiated(loggerEnumItem))
    {
        hl_logger::setLoggingLevel(hl_logger::getLogger(loggerEnumItem), newLevel);
    }
}

template<class TLoggerEnum>
void setConsoleLoggingLevel(TLoggerEnum loggerEnumItem, int newLevel)
{
    auto loggerIdx = unsigned(loggerEnumItem);
    moduleLoggerData<TLoggerEnum>.consoleLevels[loggerIdx] = newLevel;
    if (isLoggerInstantiated(loggerEnumItem))
    {
        hl_logger::setConsoleLoggingLevel(hl_logger::getLogger(loggerEnumItem), newLevel);
    }
}

template <class TLoggerEnum>
void setLazyLoggingLevel(TLoggerEnum loggerEnumItem, int newLevel)
{
    auto loggerIdx = unsigned(loggerEnumItem);
    moduleLoggerData<TLoggerEnum>.levels[loggerIdx].lazyLogLevel = newLevel;
    if (newLevel < HLLOG_LEVEL_OFF)
    {
        initLazyLoggerRecentLogsQueue(loggerEnumItem);
    }
    if (isLoggerInstantiated(loggerEnumItem))
    {
        setLoggerRecentLogsQueue(loggerEnumItem);
        hl_logger::setLazyLoggingLevel(hl_logger::getLogger(loggerEnumItem), newLevel);
    }
}

template<class TLoggerEnum>
int getLoggingLevel(TLoggerEnum loggerEnumItem)
{
    auto loggerIdx = unsigned(loggerEnumItem);
    return moduleLoggerData<TLoggerEnum>.levels[loggerIdx].logLevel;
}

template<class TLoggerEnum>
int getConsoleLoggingLevel(TLoggerEnum loggerEnumItem)
{
    auto loggerIdx = unsigned(loggerEnumItem);
    if (moduleLoggerData<TLoggerEnum>.consoleLevels[loggerIdx] != HLLOG_LEVEL_INVALID)
    {
        return moduleLoggerData<TLoggerEnum>.consoleLevels[loggerIdx];
    }
    return HLLOG_LEVEL_OFF;
}

template <class TLoggerEnum>
inline bool logLevelAtLeast(TLoggerEnum loggerEnumItem, int level)
{
    auto loggerIdx = unsigned(loggerEnumItem);
    return moduleLoggerData<TLoggerEnum>.levels[loggerIdx].logLevel <= level;
}

template <class TLoggerEnum>
inline bool lazyLogLevelAtLeast(TLoggerEnum loggerEnumItem, int level)
{
    auto loggerIdx = unsigned(loggerEnumItem);
    return moduleLoggerData<TLoggerEnum>.levels[loggerIdx].lazyLogLevel <= level;
}

template <class TLoggerEnum>
inline bool anyLogLevelAtLeast(TLoggerEnum loggerEnumItem, int level)
{
    auto loggerIdx = unsigned(loggerEnumItem);
    const auto & logLevelInfo = moduleLoggerData<TLoggerEnum>.levels[loggerIdx];
    return logLevelInfo.logLevel <= level || logLevelInfo.lazyLogLevel <= level;
}

template <class TLoggerEnum>
inline hl_logger::LoggerSPtr getLogger(TLoggerEnum loggerEnumItem)
{
    const unsigned loggerIdx = (unsigned)loggerEnumItem;
    hl_logger::LoggerSPtr logger = isLoggerInstantiated(loggerEnumItem) ? moduleLoggerData<TLoggerEnum>.loggers[loggerIdx].logger : hl_logger::LoggerSPtr();
    if (HLLOG_UNLIKELY(logger == nullptr))
    {
        if (moduleLoggerData<TLoggerEnum>.loggerOnDemandCreators[loggerIdx])
        {
            moduleLoggerData<TLoggerEnum>.loggerOnDemandCreators[loggerIdx]();
            logger = moduleLoggerData<TLoggerEnum>.loggers[loggerIdx].logger;
        }
    }
    return logger;
}

template<class TLoggerEnum>
inline bool isLoggerInstantiated(TLoggerEnum loggerEnumItem)
{
    const unsigned loggerIdx = (unsigned)loggerEnumItem;
    return moduleLoggerData<TLoggerEnum>.loggers[loggerIdx].initialized.load(std::memory_order_acquire);
}

template <class TLoggerEnum>
inline void logStacktrace(const TLoggerEnum loggerEnumItem, int level)
{
    if (logLevelAtLeast(loggerEnumItem, level))
    {
        hl_logger::logStackTrace(getLogger(loggerEnumItem), level);
    }
}

template <class TLoggerEnum>
inline SinksSPtr getSinks(const TLoggerEnum loggerEnumItem)
{
    return hl_logger::getSinks(getLogger(loggerEnumItem));
}

template<class TLoggerEnum>
HLLOG_API std::vector<std::string> getSinksFilenames(const TLoggerEnum loggerEnumItem)
{
    return hl_logger::getSinksFilenames(getLogger(loggerEnumItem));
}

template <class TLoggerEnum>
inline SinksSPtr setSinks(const TLoggerEnum loggerEnumItem, SinksSPtr sinks/* = SinksSPtr()*/)
{
    return hl_logger::setSinks(getLogger(loggerEnumItem), std::move(sinks));
}

template<class TLoggerEnum>
inline HLLOG_API void addFileSink(const TLoggerEnum loggerEnumItem,
                           std::string_view  logFileName,
                           size_t            logFileSize,
                           size_t            logFileAmount,
                           int               loggingLevel)
{
    hl_logger::addFileSink(getLogger(loggerEnumItem), logFileName, logFileSize, logFileAmount, loggingLevel);
}

template <class TLoggerEnum>
inline ResourceGuard addConsole(const TLoggerEnum loggerEnumItem)
{
    return hl_logger::addConsole(getLogger(loggerEnumItem));
}

template <class TLoggerEnum>
inline bool removeConsole(const TLoggerEnum loggerEnumItem)
{
    return hl_logger::removeConsole(getLogger(loggerEnumItem));
}

template <class TLoggerEnum>
inline void flush(const TLoggerEnum loggerEnumItem)
{
    if (isLoggerInstantiated(loggerEnumItem))
    {
        hl_logger::flush(moduleLoggerData<TLoggerEnum>.loggers[unsigned(loggerEnumItem)].logger);
    }
}

template <class TLoggerEnum>
inline void flushAll()
{
    forEachLoggerEnumItem<TLoggerEnum>([](auto loggerItem){
        flush(loggerItem);
    });
}

template<class TLoggerEnum>
inline bool drop(const TLoggerEnum loggerEnumItem)
{
    hl_logger::LoggerSPtr logger;
    if (isLoggerInstantiated(loggerEnumItem))
    {
        moduleLoggerData<TLoggerEnum>.loggers[unsigned(loggerEnumItem)].initialized.store(false, std::memory_order_release);
        logger = moduleLoggerData<TLoggerEnum>.loggers[unsigned(loggerEnumItem)].logger;
        moduleLoggerData<TLoggerEnum>.loggers[unsigned(loggerEnumItem)].logger.reset();
    }
    bool ret = logger != nullptr;
    if (logger)
    {
        if (moduleLoggerData<TLoggerEnum>.registered[unsigned(loggerEnumItem)])
        {
            dropRegisteredLogger(hl_logger::getLoggerEnumItemName(loggerEnumItem));
        }
        flush(logger);
        logger.reset();
        refreshInternalSinkCache();
    }
    return ret;
}

HLLOG_END_NAMESPACE

#define HLLOG_TO_STRING_(param) #param
#define HLLOG_TO_STRING(param) HLLOG_TO_STRING_(param)

#define HLLOG_ENUM_CASE_OP(loggerEnumItem) case HLLOG_ENUM_TYPE_NAME::loggerEnumItem: return HLLOG_TO_STRING(loggerEnumItem);
#define HLLOG_ENUM_CASES(...) HLLOG_APPLY(HLLOG_EMPTY, HLLOG_ENUM_CASE_OP, ##__VA_ARGS__)
#define HLLOG_ENUM_CASE(loggerEnumItem) case HLLOG_ENUM_TYPE_NAME::loggerEnumItem: return #loggerEnumItem;

#define HLLOG_ENUM_LIST_OP(loggerEnumItem) HLLOG_ENUM_TYPE_NAME::loggerEnumItem
#define HLLOG_ENUM_LIST(...) HLLOG_APPLY(HLLOG_COMMA, HLLOG_ENUM_LIST_OP, ##__VA_ARGS__)


#ifdef __clang__
#define HLLOG_DECLARE_MODULE_LOGGER_IMPL()                                                                             \
HLLOG_BEGIN_NAMESPACE                                                                                                  \
    template<>                                                                                                         \
    extern ModuleLoggerData<HLLOG_ENUM_TYPE_NAME> moduleLoggerData<HLLOG_ENUM_TYPE_NAME>;                              \
    template<>                                                                                                         \
    std::array<LogLevelInfo, (unsigned) HLLOG_ENUM_TYPE_NAME ::LOG_MAX> ModuleLoggerData<HLLOG_ENUM_TYPE_NAME >::levels;\
    template<>                                                                                                         \
    bool ModuleLoggerData<HLLOG_ENUM_TYPE_NAME >::scanLoggerNamesMode;                                                 \
HLLOG_END_NAMESPACE
#else
#define HLLOG_DECLARE_MODULE_LOGGER_IMPL()                                                                             \
HLLOG_BEGIN_NAMESPACE                                                                                                  \
    template<>                                                                                                         \
    std::array<LogLevelInfo, (unsigned) HLLOG_ENUM_TYPE_NAME ::LOG_MAX> ModuleLoggerData<HLLOG_ENUM_TYPE_NAME >::levels;\
    template<>                                                                                                         \
    bool ModuleLoggerData<HLLOG_ENUM_TYPE_NAME >::scanLoggerNamesMode;                                                 \
HLLOG_END_NAMESPACE
#endif

#define HLLOG_DEFINE_MODULE_LOGGER_IMPL(visibility, loggerEnumItem0, loggerEnumItem1, ...)  HLLOG_BEGIN_NAMESPACE      \
template<>                                                                                                             \
std::array<LogLevelInfo, hl_logger::getNbLoggers<HLLOG_ENUM_TYPE_NAME>()> visibility ModuleLoggerData<HLLOG_ENUM_TYPE_NAME>::levels = \
        []() constexpr {                                                                                               \
            std::array<LogLevelInfo, hl_logger::getNbLoggers<HLLOG_ENUM_TYPE_NAME>()> levels_{};                       \
            for (unsigned i = 0; i < hl_logger::getNbLoggers<HLLOG_ENUM_TYPE_NAME>(); ++i)                             \
            {                                                                                                          \
                levels_[i].logLevel     = HLLOG_LEVEL_OFF;                                                             \
                levels_[i].lazyLogLevel = HLLOG_LEVEL_OFF;                                                             \
            }                                                                                                          \
            return levels_;                                                                                            \
        }();                                                                                                           \
template<>                                                                                                             \
bool ModuleLoggerData<HLLOG_ENUM_TYPE_NAME>::scanLoggerNamesMode = false;                                              \
template<>                                                                                                             \
ModuleLoggerData<HLLOG_ENUM_TYPE_NAME> visibility moduleLoggerData<HLLOG_ENUM_TYPE_NAME>{HLLOG_TO_STRING(HLLOG_ENUM_TYPE_NAME)}; \
template<>                                                                                                             \
std::string_view visibility getLoggerEnumItemName(HLLOG_ENUM_TYPE_NAME enumItem) {                                     \
    switch(enumItem)                                                                                                   \
    {                                                                                                                  \
        HLLOG_ENUM_CASES(loggerEnumItem0, loggerEnumItem1, ##__VA_ARGS__);                                             \
    }                                                                                                                  \
    return std::string_view();                                                                                         \
}                                                                                                                      \
template <>                                                                                                            \
void forEachLoggerEnumItem<HLLOG_ENUM_TYPE_NAME>(std::function<void (HLLOG_ENUM_TYPE_NAME)> processItem) {                   \
     for (auto item : {HLLOG_ENUM_LIST(loggerEnumItem0, loggerEnumItem1, ##__VA_ARGS__) }) {                           \
         if (item != HLLOG_ENUM_TYPE_NAME::LOG_MAX) processItem(item);                                                 \
     }                                                                                                                 \
}                                                                                                                      \
HLLOG_END_NAMESPACE
