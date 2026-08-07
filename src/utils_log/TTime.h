#pragma once
#include <cstdint>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <string>
#include <iostream>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif


namespace utils
{
    namespace base
    {
        using namespace std::chrono;

        class TTime
        {
        public:
            static int64_t NowMS() {
#ifdef _WIN32
                FILETIME ft;
                ::GetSystemTimeAsFileTime(&ft);
                ULARGE_INTEGER value;
                value.LowPart = ft.dwLowDateTime;
                value.HighPart = ft.dwHighDateTime;
                // 1601-01-01 到 1970-01-01 的 100ns 间隔数，除以 10 得微秒再除 1000 得毫秒
                const int64_t kUnixEpochTicks = 116444736000000000LL;
                return (static_cast<int64_t>(value.QuadPart) - kUnixEpochTicks) / 10000;
#else
                struct timeval tv;
                gettimeofday(&tv, NULL);
                // MinGW 下 tv_sec 是 32 位 long，乘 1000 会溢出，先转 int64_t
                return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
            }
            static int64_t Now(){
#ifdef _WIN32
                return NowMS() / 1000;
#else
                struct timeval tv;
                gettimeofday(&tv, NULL);
                return tv.tv_sec;
#endif
            }
            static int64_t Now(int &year, int &month, int &day, int &hour, int &minute, int &second){
                struct tm tm;
                time_t t = time(NULL);
#ifdef _WIN32
                localtime_s(&tm, &t);
#else
                localtime_r(&t,&tm);
#endif
                year = tm.tm_year + 1900;
                month = tm.tm_mon + 1;
                day = tm.tm_mday;
                hour = tm.tm_hour;
                minute = tm.tm_min;
                second = tm.tm_sec;
                return t;
            }
            static std::string ISOTime(){
                auto now = std::chrono::system_clock::now();
                auto time_t_now = std::chrono::system_clock::to_time_t(now);
                std::tm tm_now;
#ifdef _WIN32
                gmtime_s(&tm_now, &time_t_now);
#else
                tm_now = *std::gmtime(&time_t_now);
#endif

                std::stringstream ss;
                ss << std::put_time(&tm_now,"%Y-%m-%dT%H:%M:%S");
                return ss.str();
            }
        };

    }
}
