#pragma once
#include "NoCopyable.h"
#include "FileLog.h"
#include <iostream>
#include <string>
namespace utils
{
    namespace base
    {
        enum LogLevel
        {
            kTrace = 0,
            kDebug,
            kInfo,
            kWarn,
            kError,
            kMaxNumOfLogLevel,
        };
        class Logger: public NoCopyable
        {
        public:
            Logger(const FileLogPtr &file_log);
            ~Logger() = default;
            void SetLogLevel(const LogLevel &level);
            LogLevel GetLogLevel() const;
            void Write(const std::string &msg);
        private:
            LogLevel level_{kDebug};
            FileLogPtr file_log_;
        };
    }
}