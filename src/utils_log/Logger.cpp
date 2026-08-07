#include "Logger.h"

using namespace utils::base;

utils::base::Logger::Logger(const FileLogPtr &file_log)
    : file_log_(file_log)
{
    
}

void utils::base::Logger::SetLogLevel(const LogLevel &level)
{
    level_ = level;

}

LogLevel utils::base::Logger::GetLogLevel() const
{
    return level_;
}

void utils::base::Logger::Write(const std::string &msg)
{
    std::cout << msg << std::flush;
    if(file_log_){
        file_log_->WriteLog(msg);
    }
}
