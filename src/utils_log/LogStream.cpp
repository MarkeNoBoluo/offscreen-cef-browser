#include "LogStream.h"
#include "TTime.h"
#include <cstring>
#include <string>
#include <thread>

using namespace utils::base;
static thread_local std::thread::id thread_id{};
Logger *utils::base::g_logger;
const char *log_string[] = {
    " TRACE ",
    " DEBUG ",
    " INFO  ",
    " WARN  ",
    " ERROR "
};
utils::base::LogStream::LogStream(Logger *loger, const char *file, int line, LogLevel level, const char *func)
    :logger_(loger)
{
    const char *file_name = strrchr(file, '/');
    if(file_name){
        file_name = file_name + 1;
    }else{
        file_name = file;
    }
    stream_ << TTime::ISOTime();
    if(thread_id == std::thread::id{}){
        thread_id = std::this_thread::get_id();
    }
    stream_ << thread_id;
    stream_ << log_string[level];
    stream_ << "[" << file_name << ":" << line << "]";
    if(func){
        stream_ << "[" << func << "]";
    }
}

utils::base::LogStream::~LogStream()
{
    stream_ << "\n";
    logger_->Write(stream_.str());
}
