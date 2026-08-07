#pragma once
#include "Logger.h"
#include <sstream>

namespace utils
{
    namespace base
    {
        extern Logger *g_logger;
        class LogStream
        {
        public:
            LogStream(Logger *loger, const char *file, int line, LogLevel level, const char *func);
            ~LogStream();

            template<class T> LogStream &operator<<(const T &value) {
                stream_ << value;
                return *this;
            }
        private:
            std::ostringstream stream_;
            Logger *logger_{nullptr};
        };
    }
}

// 注意：为兼容 CEF（cef_logging.h 定义了同名常量 LOG_INFO/LOG_ERROR 等），
// 日志宏统一加 UTILS_ 前缀，避免与 CEF/Chromium 日志体系冲突。
#define UTILS_LOG_TRACE  \
    if(utils::base::g_logger->GetLogLevel() <= utils::base::kTrace)  \
        utils::base::LogStream(utils::base::g_logger,__FILE__,__LINE__,utils::base::kTrace,__func__)

#define UTILS_LOG_DEBUG  \
    if(utils::base::g_logger->GetLogLevel() <= utils::base::kDebug)  \
        utils::base::LogStream(utils::base::g_logger,__FILE__,__LINE__,utils::base::kDebug,__func__)

#define UTILS_LOG_INFO    \
    if(utils::base::g_logger->GetLogLevel() <= utils::base::kInfo)   \
        utils::base::LogStream(utils::base::g_logger,__FILE__,__LINE__,utils::base::kInfo,__func__)

#define UTILS_LOG_WARN    \
    if(utils::base::g_logger->GetLogLevel() <= utils::base::kWarn)   \
        utils::base::LogStream(utils::base::g_logger,__FILE__,__LINE__,utils::base::kWarn,__func__)

#define UTILS_LOG_ERROR   \
    if(utils::base::g_logger->GetLogLevel() <= utils::base::kError)  \
        utils::base::LogStream(utils::base::g_logger,__FILE__,__LINE__,utils::base::kError,__func__)
        