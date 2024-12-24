#pragma once
#include <memory>
#include <stdint.h>
#include <string_view>
#include <string>
#include <functional>
#include <iosfwd>
#include <chrono>
#include "impl/hllog_config.hpp"
#include "impl/string_wrapper.hpp"

#define HLLOG_LEVEL_TRACE    0
#define HLLOG_LEVEL_DEBUG    1
#define HLLOG_LEVEL_INFO     2
#define HLLOG_LEVEL_WARN     3
#define HLLOG_LEVEL_ERROR    4
#define HLLOG_LEVEL_CRITICAL 5
#define HLLOG_LEVEL_OFF      6
// special values
#define HLLOG_LEVEL_INVALID  0xFF

#define HLLOG_DEFAULT_LAZY_QUEUE_SIZE 2048

namespace hl_logger{
class Logger;
using LoggerSPtr = std::shared_ptr<Logger>;

class Sinks;
using SinksSPtr = std::shared_ptr<Sinks>;

#ifndef HLLOG_USE_ABI0
inline namespace v1_4{
#else
inline namespace HLLOG_INLINE_API_NAMESPACE{
#endif
struct LoggerCreateParams
{
    std::string logFileName;                      // main log file. rotates and preserves previous log messages
    unsigned    logFileSize         = 0;          // max log file
    unsigned    logFileAmount       = 1;          // number of files for rotation
    bool        rotateLogfileOnOpen = false;      // rotate logFile on logger creation
    uint64_t    logFileBufferSize   = 0;          // default value (~5MB). if LOG_FILE_SIZE envvar is set - use its value
    std::string separateLogFile;                  // a separate log file (if needed). it's recreated on each createLogger call
    uint64_t    separateLogFileBufferSize = 0;
    bool        registerLogger      = false;      // register logger in the global registry (enable access by name from different modules)
    bool        sepLogPerThread     = false;      // separate log file per thread
    bool        printSpecialContext = false;      // print special context [C:] for each log message
    bool        printThreadID       = true;       // print tid [tid:<TID>] for each log message
    bool        printProcessID      = false;      // print pid [pid:<PID>] for each log message
    bool        forcePrintFileLine  = false;      // if false - print if PRINT_FILE_AND_LINE envvar is true
    bool        printTime           = true;       // print time field: [<TIME>] (date is configured with PRINT_DATE/PRINT_TIME
    bool        printLoggerName     = true;       // print logger name: [<LOGGER_NAME>]
    bool        printRank           = false;      // print device rank (HLS_ID, ID): [hls:<HLS_ID>][rank:<ID>]
    enum class LogLevelStyle
    {
        off,
        full_name, // [trace][debug][info][warning][error][critical]
        one_letter // [T][D][I][W][E][C]
    };
    LogLevelStyle logLevelStyle    = LogLevelStyle::full_name;
    std::string   spdlogPattern;                       // default(empty): [time][loggerName][Level] msg
    unsigned      loggerNameLength = 0;                // default(0): max length of all the logger names
    int           loggerFlushLevel = HLLOG_LEVEL_WARN; // only messages with at least loggerFlushLevel are flushed immediately
    // only messages with at least loggingLevel are printed
    // logLevel is :
    // 1. LOG_LEVEL_<LOGGER_NAME> envvar (if it's set). if it's not set see 2.
    // 2. LOG_LEVEL_ALL_<LOGGER_PREFIX> envvar (if it's set). if it's not set - defaultLogLevel
    int         defaultLoggingLevel          = HLLOG_LEVEL_CRITICAL;
    bool        forceDefaultLoggingLevel     = false;    // ignore envvars and set logLevel to defaultLogLevel
    int         defaultLazyLoggingLevel      = HLLOG_LEVEL_OFF;
    bool        forceDefaultLazyLoggingLevel = false;    // ignore envvars and set logLevel to defaultLogLevel
    uint32_t    defaultLazyQueueSize         = HLLOG_DEFAULT_LAZY_QUEUE_SIZE; // default size of lazy log messages queue
    int         defaultConsoleLoggingLevel   = HLLOG_LEVEL_OFF;
    bool        forceDefaultConsoleLoggingLevel = false;    // ignore envvars and set logLevel to defaultLogLevel
    enum class ConsoleStream
    {
        std_out,
        std_err,
        disabled // deprecated. use forceDefaultConsoleLoggingLevel = true instead
    };
    ConsoleStream consoleStream = ConsoleStream::std_out;  // type of console stream if ENABLE_CONSOLE envvar is on
};
#ifndef HLLOG_USE_ABI0
HLLOG_API LoggerSPtr createLogger(std::string_view loggerName, LoggerCreateParams const& params);
#else
}
inline namespace v1_4{
#endif

// some libraries (e.g. pytorch) require old ABI (preC++11 ABI). std::string has ABI break in C++11.
// Workaround: provide the same functionality with std::string_view (AN - ABI-neutral)
struct LoggerCreateParamsAN
{
    using string_view = std::string_view;
    string_view logFileName;                      // main log file. rotates and preserves previous log messages
    unsigned    logFileSize         = 0;          // max log file
    unsigned    logFileAmount       = 1;          // number of files for rotation
    bool        rotateLogfileOnOpen = false;      // rotate logFile on logger creation
    uint64_t    logFileBufferSize   = 0;          // default value (~5MB). if LOG_FILE_SIZE envvar is set - use its value
    string_view separateLogFile;                  // a separate log file (if needed). it's recreated on each createLogger call
    uint64_t    separateLogFileBufferSize = 0;
    bool        registerLogger      = false;      // register logger in the global registry (enable access by name from different modules)
    bool        sepLogPerThread     = false;      // separate log file per thread
    bool        printSpecialContext = false;      // print special context [C:] for each log message
    bool        printThreadID       = true;       // print tid [tid:<TID>] for each log message
    bool        printProcessID      = false;      // print pid [pid:<PID>] for each log message
    bool        forcePrintFileLine  = false;      // if false - print if PRINT_FILE_AND_LINE envvar is true
    bool        printTime           = true;       // print time field: [<TIME>] (date is configured with PRINT_DATE/PRINT_TIME
    bool        printLoggerName     = true;       // print logger name: [<LOGGER_NAME>]
    bool        printRank           = false;      // print device rank (HLS_ID, ID): [hls:<HLS_ID>][rank:<ID>]
    using LogLevelStyle = LoggerCreateParams::LogLevelStyle;
    LogLevelStyle logLevelStyle    = LogLevelStyle::full_name;
    string_view   spdlogPattern;                       // default(empty): [time][loggerName][Level] msg
    unsigned      loggerNameLength = 0;                // default(0): max length of all the logger names
    int           loggerFlushLevel = HLLOG_LEVEL_WARN; // only messages with at least loggerFlushLevel are flushed immediately
    // only messages with at least loggingLevel are printed
    // logLevel is :
    // 1. LOG_LEVEL_<LOGGER_NAME> envvar (if it's set). if it's not set see 2.
    // 2. LOG_LEVEL_ALL_<LOGGER_PREFIX> envvar (if it's set). if it's not set - defaultLogLevel
    int         defaultLoggingLevel          = HLLOG_LEVEL_CRITICAL;
    bool        forceDefaultLoggingLevel     = false;    // ignore envvars and set logLevel to defaultLogLevel
    int         defaultLazyLoggingLevel      = HLLOG_LEVEL_OFF;
    bool        forceDefaultLazyLoggingLevel = false;    // ignore envvars and set logLevel to defaultLogLevel
    uint32_t    defaultLazyQueueSize         = HLLOG_DEFAULT_LAZY_QUEUE_SIZE; // default size of lazy log messages queue
    int         defaultConsoleLoggingLevel   = HLLOG_LEVEL_OFF;
    bool        forceDefaultConsoleLoggingLevel = false;    // ignore envvars and set logLevel to defaultLogLevel
    using ConsoleStream = LoggerCreateParams::ConsoleStream;
    ConsoleStream consoleStream = ConsoleStream::std_out;  // type of console stream if ENABLE_CONSOLE envvar is on
};
HLLOG_API LoggerSPtr createLogger(std::string_view loggerName, LoggerCreateParamsAN const& params);
}

// LoggerCreateParams <-> LoggerCreateParamsAN conversions are inside inline_api namespace to avoid ABI0/ABI1 collisions
inline namespace HLLOG_INLINE_API_NAMESPACE {
inline LoggerCreateParamsAN convertLoggerCreateParamsToAN(LoggerCreateParams const & other){
    LoggerCreateParamsAN params;
    params.logFileName = other.logFileName;
    params.logFileSize = other. logFileSize;
    params.logFileAmount = other.logFileAmount;
    params.rotateLogfileOnOpen = other.rotateLogfileOnOpen;
    params.logFileBufferSize = other.logFileBufferSize;
    params.separateLogFile = other.separateLogFile;
    params.separateLogFileBufferSize = other.separateLogFileBufferSize;
    params.registerLogger = other.registerLogger;
    params.sepLogPerThread = other.sepLogPerThread;
    params.printSpecialContext = other.printSpecialContext;
    params.printThreadID = other.printThreadID;
    params.printProcessID = other.printProcessID;
    params.forcePrintFileLine = other.forcePrintFileLine;
    params.printTime = other.printTime;
    params.printLoggerName = other.printLoggerName;
    params.printRank = other.printRank;
    params.logLevelStyle = other.logLevelStyle;
    params.spdlogPattern = other.spdlogPattern;
    params.loggerNameLength = other.loggerNameLength;
    params.loggerFlushLevel = other.loggerFlushLevel;
    params.defaultLoggingLevel = other.defaultLoggingLevel;
    params.forceDefaultLoggingLevel = other.forceDefaultLoggingLevel;
    params.defaultLazyLoggingLevel = other.defaultLazyLoggingLevel;
    params.forceDefaultLazyLoggingLevel = other.forceDefaultLazyLoggingLevel;
    params.defaultLazyQueueSize = other.defaultLazyQueueSize;
    params.defaultConsoleLoggingLevel = other.defaultConsoleLoggingLevel;
    params.forceDefaultConsoleLoggingLevel = other.forceDefaultConsoleLoggingLevel;
    params.consoleStream = other.consoleStream;
    return params;
}
inline LoggerCreateParams convertToLoggerCreateParamsFromAN(LoggerCreateParamsAN const & other){
    LoggerCreateParams params;
    params.logFileName = other.logFileName;
    params.logFileSize = other.logFileSize;
    params.logFileAmount = other.logFileAmount;
    params.rotateLogfileOnOpen = other.rotateLogfileOnOpen;
    params.logFileBufferSize = other.logFileBufferSize;
    params.separateLogFile = other.separateLogFile;
    params.separateLogFileBufferSize = other.separateLogFileBufferSize;
    params.registerLogger = other.registerLogger;
    params.sepLogPerThread = other.sepLogPerThread;
    params.printSpecialContext = other.printSpecialContext;
    params.printThreadID = other.printThreadID;
    params.printProcessID = other.printProcessID;
    params.forcePrintFileLine = other.forcePrintFileLine;
    params.printTime = other.printTime;
    params.printLoggerName = other.printLoggerName;
    params.printRank = other.printRank;
    params.logLevelStyle = other.logLevelStyle;
    params.spdlogPattern = other.spdlogPattern;
    params.loggerNameLength = other.loggerNameLength;
    params.loggerFlushLevel = other.loggerFlushLevel;
    params.defaultLoggingLevel = other.defaultLoggingLevel;
    params.forceDefaultLoggingLevel = other.forceDefaultLoggingLevel;
    params.defaultLazyLoggingLevel = other.defaultLazyLoggingLevel;
    params.forceDefaultLazyLoggingLevel = other.forceDefaultLazyLoggingLevel;
    params.defaultLazyQueueSize = other.defaultLazyQueueSize;
    params.defaultConsoleLoggingLevel = other.defaultConsoleLoggingLevel;
    params.forceDefaultConsoleLoggingLevel = other.forceDefaultConsoleLoggingLevel;
    params.consoleStream = other.consoleStream;
    return params;
}
}

inline namespace v1_0{
// deprecated. will be removed in future versions
const uint8_t defaultLoggingLevel = 0xFF;
class [[nodiscard]] ResourceGuard
{
public:
    ResourceGuard() : _releaseResourceFunc(nullptr) {}
    explicit ResourceGuard(std::function<void()> releaseResourceFunc) : _releaseResourceFunc(std::move(releaseResourceFunc)) {}
    ResourceGuard(ResourceGuard const&) = delete;
    ResourceGuard(ResourceGuard&& other)
    {
        _releaseResourceFunc       = std::move(other._releaseResourceFunc);
        other._releaseResourceFunc = nullptr;
    }
    ResourceGuard& operator=(ResourceGuard&& other)
    {
        _releaseResourceFunc       = std::move(other._releaseResourceFunc);
        other._releaseResourceFunc = nullptr;
        return *this;
    }

    ~ResourceGuard()
    {
        if (_releaseResourceFunc) _releaseResourceFunc();
    };

    bool     isValid() const { return _releaseResourceFunc != nullptr; }
    explicit operator bool() const { return isValid(); }

private:
    std::function<void()> _releaseResourceFunc;
};

/**
 * @brief getRegisteredLogger get a registered logger by name.
 *        the logger must be created with registerLogger = true
 *        this function is mainly to support string-based api (hllog_se.hpp)
 * @param loggerName
 * @return logger
 */
HLLOG_API LoggerSPtr getRegisteredLogger(std::string_view loggerName);

/**
 * @brief dropRegisteredLogger remove a registered logger from the internal registry.
 *        the logger is destroyed if it's not kept by the user code
 *        this function is mainly to support string-based api (hllog_se.hpp)
 * @param loggerName
 */
HLLOG_API void dropRegisteredLogger(std::string_view loggerName);

/**
 * @brief dropAllRegisteredLoggers drop all the registered loggers.
 *        see dropRegisteredLogger
 */
HLLOG_API void dropAllRegisteredLoggers();

/**
 * @brief refreshInternalSinkCache is called internal after dropping a logger
 *        to refresh internal data structures. usually no need to call it from the user side
 */
HLLOG_API void refreshInternalSinkCache();

/**
 * @brief  setLoggingLevelByMask set minimal enabled logging message level for loggers from all modules by mask
 *
 * @param loggerNameMask - the same structure as env vars: LOG_LEVEL_ALL, LOG_LEVEL_PREFIX_ALL, LOG_LEVEL_NAME
 * @param newLevel new logging level
 */
HLLOG_API void setLoggingLevelByMask(std::string_view loggerNameMask, int newLevel);

/**
 * @brief  getLoggingLevelByName get level for a logger from all modules by name
 *
 * @param loggerName - a string name of a logger
 * @return logging level of a logger or HLLOG_LEVEL_INVALID if the logger not found
 */
HLLOG_API int getLoggingLevelByName(std::string_view loggerName);

/**
 * @brief  setLoggingLevel set minimal enabled message level for logging into a logger
 *
 * @param logger
 * @param newLevel new logging level
 */
HLLOG_API void setLoggingLevel(LoggerSPtr const& logger, int newLevel);

/**
 * @brief  setConsoleLoggingLevel set logging level for console (if console not enabled - no effect)
 *
 * @param logger
 * @param newLevel new console logging level
 */
HLLOG_API void setConsoleLoggingLevel(LoggerSPtr const& logger, int newLevel);

/**
 * @brief  setLazyLoggingLevel set minimal enabled message level for lazy logging into a logger
 *
 * @param logger
 * @param newLevel new lazy logging level
 */
HLLOG_API void setLazyLoggingLevel(LoggerSPtr const& logger, int newLevel);

/**
 * @brief get logging level of the logger
 * @param logger
 * @return current logging level
 */
HLLOG_API int getLoggingLevel(LoggerSPtr const& logger);

/**
 * @brief get console logging level of the logger
 * @param logger
 * @return current console logging level
 */
HLLOG_API int getConsoleLoggingLevel(LoggerSPtr const& logger);

/**
 * @brief get lazy logging level of the logger
 * @param logger
 * @return current lazy logging level
 */
HLLOG_API int getLazyLoggingLevel(LoggerSPtr const& logger);

/**
 * @bried flush a logger
 * @param logger
 */
HLLOG_API void flush(LoggerSPtr const& logger);

/**
 * @bried flush all loggers
 */
HLLOG_API void flush();

/**
 * @brief enablePeriodicFlush flush all the loggers periodically. For the following scenario:
 * 1. a logger is created with loggerFlushLevel higher than HLLOG_LEVEL_TRACE
 * 2. an app is killed with a sigkill (all unflushed messages are lost)
 *
 * periodic flush is off by default because in some scenarios (related to fork) periodic flush causes issues
 * enable periodic flush when it's safe (e.g. synInitialize) and disable accordingly (e.g. synDestroy)
 * @param enable true enable, false - disable
 */
HLLOG_API void enablePeriodicFlush(bool enable = true);

/**
 * @brief addFileSink add a file sink to an existing logger
 *        this function is mainly to support string-based api (hllog_se.hpp)
 *        NOT THREAD SAFE
 * @param logger
 * @param logFileName
 * @param logFileSize
 * @param logFileAmount
 * @param loggingLevel  logging level of the new file sink, by default it's equal to the logging level of the logger
 */
HLLOG_API void addFileSink(LoggerSPtr const& logger,
                           std::string_view  logFileName,
                           size_t            logFileSize,
                           size_t            logFileAmount,
                           int               loggingLevel = HLLOG_LEVEL_INVALID);

/**
 * @brief getSinks get logger sinks
 *        NOT THREAD SAFE
 * @param logger
 * @return sinks of the logger
 */
HLLOG_API SinksSPtr getSinks(LoggerSPtr const& logger);

#ifndef HLLOG_USE_ABI0
/**
 * @brief getSinksFilenames
 *        NOT THREAD SAFE
 * @param logger
 * @return filenames of file_sinks that are connected to the logger
 */
HLLOG_API std::vector<std::string> getSinksFilenames(LoggerSPtr const& logger);
#endif
/**
 * @brief setSinks set new logger sinks and return old ones
 *        NOT THREAD SAFE
 * @param logger
 * @param sinks new sinks for the logger
 * @return previous sinks of the logger
 */
HLLOG_API SinksSPtr setSinks(LoggerSPtr const& logger, SinksSPtr sinks = SinksSPtr());

/**
 * @brief addConsole add a console sinks to a logger
 *        NOT THREAD SAFE
 * @param logger
 * @return ResourceGuard that will remove added console in its dtor
 */
HLLOG_API ResourceGuard addConsole(LoggerSPtr const& logger);

/**
 * @brief log log a message into a logger with logLevel
 * @param logger
 * @param logLevel message log level
 * @param forcePrint force printing regardless of the logger logging level
 * @param msg  message
 * @param file filename of the log message source code
 * @param line line number of the log message source code
 * @param forcePrintFileLine force printing file and line. if false - according to logger params
 */
HLLOG_API void log(LoggerSPtr const& logger,
                   int               logLevel,
                   bool              forcePrint,
                   std::string_view  msg,
                   std::string_view  file = std::string_view(),
                   int               line = 0,
                   bool              forcePrintFileLine = false);
HLLOG_API void log(LoggerSPtr const& logger,
                   int               logLevel,
                   std::string_view  msg,
                   std::string_view  file = std::string_view(),
                   int               line = 0,
                   bool              forcePrintFileLine = false);

/**
 * @brief logStackTrace log stacktrace
 * @param logger
 * @param logLevel
 */
HLLOG_API void logStackTrace(LoggerSPtr const& logger, int logLevel);

/**
 * @brief logStackTrace log stacktrace into an ostream
 * @param ostream  output stream
 */
HLLOG_API void logStackTrace(std::ostream & ostream);

/**
 * log all the lazy logs that are kept in memory into a file
 * @param filename
 */
HLLOG_API void logAllLazyLogs(std::string_view filename);

/**
 * log all the lazy logs that are kept in memory into a logger
 * @param logger
 */
HLLOG_API void logAllLazyLogs(LoggerSPtr logger);

/**
 * @brief getDefaultLoggingLevel get logger level according to env variables
 * @param loggerName
 * @param defaultLevel default logging level - it's used if no env vars found related to this loggerName
 * @return log level
 */
HLLOG_API uint8_t getDefaultLoggingLevel(std::string_view loggerName, int defaultLevel);

/**
 * @brief getDefaultLazyLoggingLevel get lazy logger level according to env variables
 * @param loggerName
 * @param defaultLevel default logging level - it's used if no env vars found related to this loggerName
 * @return lazy log level
 */
HLLOG_API uint8_t getDefaultLazyLoggingLevel(std::string_view loggerName, int defaultLevel);

/**
 * @brief getDefaultConsoleLoggingLevel get console logger level according to env variables
 * @param loggerName
 * @param defaultLevel default logging level - it's used if no env vars found related to this loggerName
 * @return console log level
 */
HLLOG_API uint8_t getDefaultConsoleLoggingLevel(std::string_view loggerName, int defaultLevel);

/**
 * @brief getLazyQueueSize get lazy log messages queue size according to env variables
 *        lazy queue size defines the number of log messages that are saved for lazy logs
 * @param loggerName
 * @param defaultQueueSize default queue size - it's used if no env vars found related to this loggerName
 * @return lazy queue size
 */
HLLOG_API uint32_t getLazyQueueSize(std::string_view loggerName, uint32_t defaultQueueSize);

/**
 * @brief getLogsFolderPath
 * @return current logs folder
 */
HLLOG_API std::string getLogsFolderPath();
// ABI-neutral version
HLLOG_API string_wrapper getLogsFolderPathAN();

/**
 * @brief getLogsFolderPath
 * @return logs folder according to env vars
 */
HLLOG_API std::string getLogsFolderPathFromEnv();
// ABI-neutral version
HLLOG_API string_wrapper getLogsFolderPathFromEnvAN();
/**
 * @brief Changes logs directory for all existing loggers which have been already
 *        initialized.
 * @param logsDir new logs directory
 */
HLLOG_API void setLogsFolderPath(std::string_view logsDir);

/**
 * @brief Changes logs directory to path determined based on env variables
 *        (returned by hl_logger::getLogsFolderPath()).
 */
HLLOG_API void setLogsFolderPathFromEnv();

/**
 * @brief addCurThreadGlobalContext add global context to the current thread
 *        global context is printed in all the loggers. format [C:context]
 *        usually is used to mark all the underlying log messages with some tag
 * @param threadContext a context message
 */
HLLOG_API void addCurThreadGlobalContext(std::string_view threadContext);

/**
 * @brief removeCurThreadGlobalContext
 */
HLLOG_API void removeCurThreadGlobalContext();

/**
 * @brief addCurThreadSpecialContext add special context to the current thread
 *        special context is printed in loggers that created with printSpecialContext = true
 * @param threadContext a context message
 */
HLLOG_API void addCurThreadSpecialContext(std::string_view threadContext);

/**
 * @brief removeCurThreadSpecialContext
 */
HLLOG_API void removeCurThreadSpecialContext();

/**
 * @brief enableTraceMode enable trace mode for the current thread.
 *        in trace mode all the log messages will be logged as HLLOG_LEVEL_TRACE
 *        usually it's used to supress error messages if they are expected
 * @param enableTraceMode
 */
 // TODO: rethink the naming. it should be message log level ?
HLLOG_API void enableTraceMode(bool enableTraceMode);
}
inline namespace v1_1{
struct VersionInfo
{
    string_wrapper commitSHA1;
};

/**
 * @brief get library version info
 * @return VersionInfo
 */
HLLOG_API VersionInfo getVersion();
}
inline namespace v1_0{
using SignalHandlerV2 = std::function<void(int signal, const char* signalStr, bool isSevere)>;
HLLOG_API ResourceGuard registerSignalHandler(SignalHandlerV2 signalHandler, std::string_view moduleName);

using FlushHandler = std::function<void()>;
HLLOG_API ResourceGuard registerFlushHandler(FlushHandler flushHandler, std::string_view moduleName);

[[deprecated("use an overload with moduleName")]] HLLOG_API ResourceGuard registerSignalHandler(SignalHandlerV2 signalHandler);
[[deprecated("use an overload with moduleName")]] HLLOG_API ResourceGuard registerFlushHandler(FlushHandler flushHandler);
using SignalHandler = std::function<void(int signal, const char* signalStr)>;
[[deprecated("use V2 overload")]] HLLOG_API ResourceGuard registerSignalHandler(SignalHandler signalHandler);

};  // namespace vXX
}  // namespace hl_logger
