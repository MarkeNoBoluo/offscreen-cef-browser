#pragma once
#include <string>
#include <memory>
#include <cstdint>
#include <mutex>
namespace utils
{
    namespace base
    {
        enum RotateType
        {
            kRotateNone,
            kRotateMinute,
            kRotateHour,
            kRotateDay,

        };
        class LogFile
        {
        public:
            LogFile() = default;
            explicit LogFile(const std::string &filePath) : file_path_(filePath) {}
            ~LogFile() = default;

            bool Open(const std::string &filePath);
            size_t WriteLog(const std::string &msg);
            void Rotate(const std::string &file);
            void SetRotate(RotateType type);
            RotateType GetRotateType() const;
            int64_t FileSize() const;
            std::string FilePath() const;
        private:
            /// 假设 mutex_ 已持有，打开（或重开）日志文件；重开时先关闭旧句柄。
            bool OpenLocked(const std::string &filePath);
            int fd_{-1};
            std::string file_path_;
            RotateType rotate_type_{kRotateNone};
            mutable std::mutex mutex_;
        };
        using FileLogPtr = std::shared_ptr<utils::base::LogFile>;
    }
}