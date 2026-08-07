#include "FileLog.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <cerrno>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif
#include <iostream>

using namespace utils::base;

#ifdef _WIN32
namespace {

/// 将 UTF-8 路径转换为宽字符路径，供 Win32 API 使用。
/// @param utf8_path UTF-8 编码的路径。
/// @return 宽字符路径；转换失败时为空字符串。
std::wstring Utf8ToWidePath(const std::string& utf8_path) {
  if (utf8_path.empty()) {
    return std::wstring();
  }
  const int length = ::MultiByteToWideChar(
      CP_UTF8, 0, utf8_path.c_str(), static_cast<int>(utf8_path.size()),
      nullptr, 0);
  if (length <= 0) {
    return std::wstring();
  }
  std::wstring wide(length, L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, utf8_path.c_str(),
                        static_cast<int>(utf8_path.size()), &wide[0], length);
  return wide;
}

}  // namespace
#endif

bool LogFile::Open(const std::string &filePath)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return OpenLocked(filePath);
}

bool LogFile::OpenLocked(const std::string &filePath)
{
    file_path_ = filePath;
#ifdef _WIN32
    // 重开前先关闭旧句柄（支持按小时切换文件路径；重开失败时旧句柄已释放）
    const HANDLE old_handle = reinterpret_cast<HANDLE>(
        static_cast<intptr_t>(fd_));
    if (old_handle != INVALID_HANDLE_VALUE && old_handle != nullptr) {
        ::CloseHandle(old_handle);
        fd_ = -1;
    }
    const std::wstring wide_path = Utf8ToWidePath(filePath);
    if (wide_path.empty()) {
        std::cout << "open file " << file_path_ << " failed" << std::endl;
        return false;
    }
    const HANDLE handle = ::CreateFileW(
        wide_path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        std::cout << "open file " << file_path_ << " failed, error="
                  << ::GetLastError() << std::endl;
        return false;
    }
    fd_ = static_cast<int>(reinterpret_cast<intptr_t>(handle));
    return true;
#else
    if(fd_ >= 0){
        ::close(fd_);
        fd_ = -1;
    }
    int fd = ::open(file_path_.c_str(), O_CREAT | O_APPEND | O_WRONLY , 0666);
    if(fd < 0){
        std::cout << "open file " << file_path_ << " failed" << std::endl;
        return false;
    }
    fd_ = fd;
    return true;
#endif
}

size_t LogFile::WriteLog(const std::string &msg)
{
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    const HANDLE handle = reinterpret_cast<HANDLE>(
        static_cast<intptr_t>(fd_));
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
        return 0;
    }
    DWORD bytes_written = 0;
    ::WriteFile(handle, msg.data(), static_cast<DWORD>(msg.size()),
                &bytes_written, nullptr);
    return static_cast<size_t>(bytes_written);
#else
    int fd = fd_==-1?1:fd_;
    return ::write(fd, msg.c_str(), msg.size());
#endif
}

void LogFile::Rotate(const std::string &file)
{
    if(file_path_.empty()){
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    // Windows 下 MoveFile 一个仍被打开的文件会因共享冲突失败，先关闭句柄
    const HANDLE handle = reinterpret_cast<HANDLE>(
        static_cast<intptr_t>(fd_));
    if (handle != INVALID_HANDLE_VALUE && handle != nullptr) {
        ::CloseHandle(handle);
        fd_ = -1;
    }
    const std::wstring from_path = Utf8ToWidePath(file_path_);
    const std::wstring to_path = Utf8ToWidePath(file);
    if (!from_path.empty() && !to_path.empty()) {
        if (!::MoveFileW(from_path.c_str(), to_path.c_str())) {
            std::cerr << "rename file " << file_path_ << " to " << file
                      << " failed, error=" << ::GetLastError() << std::endl;
        }
    }
    // 移动无论成败都重新打开原路径，保证后续日志能继续写入
    OpenLocked(file_path_);
#else
    // Windows 下 rename 一个仍被打开的文件会因共享冲突失败，先关闭 fd
    if(fd_ >= 0){
        ::close(fd_);
        fd_ = -1;
    }
    int ret = ::rename(file_path_.c_str(), file.c_str());
    if(ret != 0){
        std::cerr << "rename file " << file_path_ << " to " << file << " failed, errno=" << errno << std::endl;
    }
    // rename 无论成败都重新打开原路径，保证后续日志能继续写入
    int fd = ::open(file_path_.c_str(), O_CREAT | O_APPEND | O_WRONLY , 0666);
    if(fd < 0){
        std::cerr << "open file " << file_path_ << " failed" << std::endl;
        return;
    }
    fd_ = fd;
#endif
}

void LogFile::SetRotate(RotateType type)
{
    rotate_type_ = type;
}

RotateType LogFile::GetRotateType() const
{
    return rotate_type_;
}

int64_t LogFile::FileSize() const
{
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _WIN32
    const HANDLE handle = reinterpret_cast<HANDLE>(
        static_cast<intptr_t>(fd_));
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
        return -1;
    }
    LARGE_INTEGER size = {};
    if (!::GetFileSizeEx(handle, &size)) {
        return -1;
    }
    return static_cast<int64_t>(size.QuadPart);
#else
    return ::lseek64(fd_, 0, SEEK_END);
#endif
}

std::string LogFile::FilePath() const
{
    return file_path_;
}
