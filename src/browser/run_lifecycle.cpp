#include "browser/run_lifecycle.h"

#include <iomanip>
#include <mutex>
#include <sstream>

#include <windows.h>

#include "app/diagnostic_log.h"

namespace offscreen {

namespace {

std::mutex& RunLifecycleMutex() {
  static std::mutex run_mutex;
  return run_mutex;
}

/// 当前运行实例的 run_id；未 Begin 时为空。
std::string& RunIdState() {
  static std::string run_id;
  return run_id;
}

/// 当前运行实例的启动时刻（ISO 8601）；未 Begin 时为空。
std::string& RunStartState() {
  static std::string run_start;
  return run_start;
}

/// 返回 ISO 8601 本地时间 YYYY-MM-DDTHH:MM:SS。
/// @return 当前时刻文本。
std::string FormatIsoNow() {
  SYSTEMTIME st;
  ::GetLocalTime(&st);
  std::ostringstream stream;
  stream << std::setfill('0') << std::setw(4) << st.wYear << '-' << std::setw(2)
         << st.wMonth << '-' << std::setw(2) << st.wDay << 'T' << std::setw(2)
         << st.wHour << ':' << std::setw(2) << st.wMinute << ':' << std::setw(2)
         << st.wSecond;
  return stream.str();
}

/// 返回 run_id：YYYYMMDD_HHMMSS（本地时间）。
/// @return run_id 文本。
std::string FormatRunIdNow() {
  SYSTEMTIME st;
  ::GetLocalTime(&st);
  std::ostringstream stream;
  stream << std::setfill('0') << std::setw(4) << st.wYear << std::setw(2)
         << st.wMonth << std::setw(2) << st.wDay << '_' << std::setw(2)
         << st.wHour << std::setw(2) << st.wMinute << std::setw(2) << st.wSecond;
  return stream.str();
}

/// 可执行文件所在目录（UTF-8），用于定位生命周期 CSV。
/// @return 目录路径（含结尾分隔符）；失败时为空。
std::string ApplicationDirectoryUtf8() {
  wchar_t module_path[MAX_PATH] = {};
  const DWORD length = ::GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    return std::string();
  }
  std::wstring dir(module_path, length);
  const size_t separator = dir.find_last_of(L"\\/");
  if (separator == std::wstring::npos) {
    return std::string();
  }
  dir.resize(separator + 1);
  const int utf8_length = ::WideCharToMultiByte(
      CP_UTF8, 0, dir.c_str(), static_cast<int>(dir.size()), nullptr, 0,
      nullptr, nullptr);
  if (utf8_length <= 0) {
    return std::string();
  }
  std::string utf8(static_cast<std::size_t>(utf8_length), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, dir.c_str(),
                        static_cast<int>(dir.size()), utf8.data(), utf8_length,
                        nullptr, nullptr);
  return utf8;
}

/// 向生命周期 CSV 追加一行；文件为空时先写 UTF-8 BOM + 表头。
/// @param dir 目录（UTF-8，含结尾分隔符）。
/// @param line 待追加的 CSV 行（含末尾换行）。
void AppendLine(const std::string& dir, const std::string& line) {
  if (dir.empty()) {
    return;
  }
  const std::string path = dir + "offscreen_run_lifecycle_" +
                           FormatHourKey() + ".csv";
  const int wide_length = ::MultiByteToWideChar(
      CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), nullptr, 0);
  if (wide_length <= 0) {
    return;
  }
  std::wstring wide_path(static_cast<std::size_t>(wide_length), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, path.c_str(),
                        static_cast<int>(path.size()), &wide_path[0],
                        wide_length);

  const HANDLE file = ::CreateFileW(
      wide_path.c_str(), FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  if (::GetFileSize(file, nullptr) == 0) {
    static const char kUtf8Bom[] = "\xEF\xBB\xBF";
    DWORD bytes_written = 0;
    ::WriteFile(file, kUtf8Bom, 3, &bytes_written, nullptr);
    static const char kHeader[] =
        "run_id,run_start,run_end,exit_code,shutdown_completed\n";
    ::WriteFile(file, kHeader, sizeof(kHeader) - 1, &bytes_written, nullptr);
  }
  DWORD bytes_written = 0;
  ::WriteFile(file, line.data(), static_cast<DWORD>(line.size()),
              &bytes_written, nullptr);
  ::CloseHandle(file);
}

}  // namespace

std::string RunLifecycleId() {
  std::lock_guard<std::mutex> lock(RunLifecycleMutex());
  return RunIdState();
}

std::string RunLifecycleStartTime() {
  std::lock_guard<std::mutex> lock(RunLifecycleMutex());
  return RunStartState();
}

void RunLifecycleBegin() {
  std::lock_guard<std::mutex> lock(RunLifecycleMutex());
  if (!RunIdState().empty()) {
    return;  // 已初始化，忽略重复调用。
  }
  RunIdState() = FormatRunIdNow();
  RunStartState() = FormatIsoNow();
  AppendLine(ApplicationDirectoryUtf8(),
             RunIdState() + "," + RunStartState() + ",,,0\n");
  DiagnosticLog("RunLifecycle begin run_id=[" + RunIdState() +
                "] run_start=[" + RunStartState() + "]");
}

void RunLifecycleEnd(int exit_code, bool shutdown_completed) {
  std::lock_guard<std::mutex> lock(RunLifecycleMutex());
  if (RunIdState().empty()) {
    return;
  }
  const std::string run_id = RunIdState();
  const std::string run_start = RunStartState();
  const std::string run_end = FormatIsoNow();
  AppendLine(ApplicationDirectoryUtf8(),
             run_id + "," + run_start + "," + run_end + "," +
                 std::to_string(exit_code) + "," +
                 (shutdown_completed ? "1" : "0") + "\n");
  DiagnosticLog("RunLifecycle end run_id=[" + run_id + "] run_end=[" +
                run_end + "] exit_code=" + std::to_string(exit_code) +
                " shutdown_completed=" + (shutdown_completed ? "1" : "0"));
}

}  // namespace offscreen
