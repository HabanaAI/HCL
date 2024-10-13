#include <algorithm>
#include <cstdlib>

#include "dfa_defines.hpp"
#include "hcl_log_manager.h"

#define LOG_SIZE   200 * 1024 * 1024
#define LOG_AMOUNT 5

#define HCL_LOG_FILE               "hcl.log"
#define HCL_COORDINATOR_LOG_FILE   "hcl_coordinator.log"
#define HCL_TEST_SEPARATE_LOG_FILE "hcl_test.log"
namespace hcl
{
LogManager& LogManager::instance()
{
    static LogManager instance;

    return instance;
}

static void createModuleLoggers(LogManager::LogType)
{
    hl_logger::LoggerCreateParams hclParams;
    hclParams.logFileName         = HCL_LOG_FILE;
    hclParams.logFileSize         = LOG_SIZE;
    hclParams.logFileAmount       = LOG_AMOUNT;
    hclParams.logFileBufferSize   = 1024 * 1024;
    hclParams.printSpecialContext = true;
    hclParams.rotateLogfileOnOpen = true;
    hl_logger::createLoggers({LogManager::LogType::HCL,
                              LogManager::LogType::HCL_OFI,
                              LogManager::LogType::HCL_SCAL,
                              LogManager::LogType::HCL_SIMB,
                              LogManager::LogType::HCL_SYNCOB,
                              LogManager::LogType::HCL_ECR,
                              LogManager::LogType::HCL_PROACT,
                              LogManager::LogType::HCL_IBV,
                              LogManager::LogType::FUNC_SCOPE,
                              LogManager::LogType::HCL_SUBMIT},
                             hclParams);
    // save HCL_API INFO logs to the lazy log
    hclParams.defaultLazyLoggingLevel = HLLOG_LEVEL_INFO;
    hl_logger::createLogger(LogManager::LogType::HCL_API, hclParams);
}

static void createModuleLoggersOnDemand(LogManager::LogType)
{
    // hcl coordinator
    {
        hl_logger::LoggerCreateParams hclParams;
        hclParams.logFileName         = HCL_COORDINATOR_LOG_FILE;
        hclParams.logFileSize         = LOG_SIZE;
        hclParams.logFileAmount       = LOG_AMOUNT;
        hclParams.logFileBufferSize   = 1024 * 1024;
        hclParams.printSpecialContext = true;
        hl_logger::createLoggerOnDemand(LogManager::LogType::HCL_COORD, hclParams);
    }

    // hcl test
    {
        hl_logger::LoggerCreateParams hclParams;
        hclParams.logFileName         = HCL_LOG_FILE;
        hclParams.logFileSize         = LOG_SIZE;
        hclParams.logFileAmount       = LOG_AMOUNT;
        hclParams.logFileBufferSize   = 1024 * 1024;
        hclParams.printSpecialContext = true;
        hclParams.separateLogFile     = HCL_TEST_SEPARATE_LOG_FILE;
        hl_logger::createLoggerOnDemand(LogManager::LogType::HCL_TEST, hclParams);
    }

    // work progress
    {
        hl_logger::LoggerCreateParams workProgressParams;

        workProgressParams.logFileName                  = HCL_LOG_FILE;
        workProgressParams.logFileSize                  = LOG_SIZE;
        workProgressParams.logFileAmount                = LOG_AMOUNT;
        workProgressParams.logFileBufferSize            = 1024 * 1024;
        workProgressParams.defaultLazyLoggingLevel      = HLLOG_LEVEL_TRACE;
        workProgressParams.defaultLoggingLevel          = HLLOG_LEVEL_OFF;
        workProgressParams.forceDefaultLazyLoggingLevel = true;
        workProgressParams.consoleStream                = hl_logger::LoggerCreateParams::ConsoleStream::disabled;

        hl_logger::createLoggerOnDemand(LogManager::LogType::HCL_CG, workProgressParams);
    }
}

LogManager::LogManager() {}

LogManager::~LogManager() {}

void LogManager::enablePeriodicFlush(bool enable)
{
    hl_logger::enablePeriodicFlush(enable);
}

void LogManager::set_log_level(const LogManager::LogType& logType, unsigned log_level)
{
    hl_logger::setLoggingLevel(logType, log_level);
}

// For testing: allows the caller to change the sink. Either to the given logSinks (if exists),
// if not, then to the given file
LogManager::LogSinks LogManager::setLogSinks(const LogManager::LogType& logType, LogSinks logSinks)
{
    hl_logger::flush(logType);
    return hl_logger::setSinks(logType, std::move(logSinks));
}

LogManager::LogSinks LogManager::getLogSinks(const LogManager::LogType& logType) const
{
    return hl_logger::getSinks(logType);
}

void LogManager::setLogSinks(const LogManager::LogType& logType, const std::string& newLogFileName)
{
    hl_logger::setSinks(logType);
    hl_logger::addFileSink(logType, newLogFileName, 2 * 1024 * 1024 * 1024ull, 0);
}

bool LogManager::getLogsFolderPath(std::string& logsFolderPath)
{
    logsFolderPath = hl_logger::getLogsFolderPath();
    return !logsFolderPath.empty();
}
void LogManager::flush()
{
    hl_logger::flushAll<LogType>();
}

void LogManager::set_logger_sink(const LogType&     logType,
                                 const std::string& pathname,
                                 unsigned           lvl,
                                 size_t             size,
                                 size_t             amount)
{
    hl_logger::addFileSink(logType, pathname, size, amount, lvl);
}

void LogManager::log_wrapper(const LogManager::LogType& logType, const int logLevel, std::string&& s)
{
    HLLOG_TYPED_PREFIXED(logType, logLevel, "{}", s);
}

void LogManager::setLogContext(const std::string& logContext)
{
    hl_logger::addCurThreadSpecialContext(logContext);
}

void LogManager::clearLogContext()
{
    hl_logger::removeCurThreadSpecialContext();
}

FuncScopeLog::FuncScopeLog(const std::string& function) : m_function(function)
{
    LOG_TRACE(FUNC_SCOPE, "{} - function begin", m_function);
}

FuncScopeLog::~FuncScopeLog()
{
    LOG_TRACE(FUNC_SCOPE, "{} - function end", m_function);
}

}  // namespace hcl

// all logger names should be limited to HCL_ + max 6 chars
HLLOG_DEFINE_MODULE_LOGGER(HCL_API,
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
                           LOG_MAX)

namespace hcl
{
// functions use specialization of ModuleLoggerData that's why they must be after HLLOG_DEFINE_MODULE_LOGGER
const std::string_view LogManager::getLogTypeString(const LogType& logType) const
{
    return hl_logger::getLoggerEnumItemName(logType);
}

void LogManager::create_logger(const LogManager::LogType& logType,
                               const std::string&         fileName,
                               unsigned                   logFileSize,
                               unsigned                   logFileAmount,
                               const char*                separateLogFile,
                               bool                       sepLogPerThread)
{
    hl_logger::LoggerCreateParams params;
    params.logFileName      = fileName;
    params.logFileAmount    = logFileAmount;
    params.logFileSize      = logFileSize;
    params.separateLogFile  = separateLogFile ? separateLogFile : "";
    params.sepLogPerThread  = sepLogPerThread;
    params.loggerFlushLevel = HLLOG_LEVEL_WARN;
    params.defaultLoggingLevel =
        hl_logger::getLoggingLevel(logType) != HLLOG_LEVEL_OFF ? hl_logger::defaultLoggingLevel : HLLOG_LEVEL_ERROR;

    auto loggerName = hl_logger::getLoggerEnumItemName(logType);

    auto logger = hl_logger::getLogger(logType);
    if (logger != nullptr)
    {
        hl_logger::log(logger, HLLOG_LEVEL_CRITICAL, fmt::format("Logger was redefined {}", loggerName));
        return;
    }

    hl_logger::createLogger(logType, params);
}

}  // namespace hcl
