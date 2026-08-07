#pragma once

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <windows.h>

#include "utils_log/Logger.h"
#include "utils_log/LogStream.h"

namespace offscreen {

/// 返回诊断日志共享锁，串行化多线程写入。
/// @return 进程内唯一的互斥量。
inline std::mutex& DiagnosticLogMutex() {
  static std::mutex log_mutex;
  return log_mutex;
}

/// 返回当前诊断日志文件句柄的存储位置（兼容旧接口，不再直接持有句柄）。
/// @return 可读写的进程级文件句柄引用。
inline HANDLE& DiagnosticLogFileHandle() {
  static HANDLE log_file = INVALID_HANDLE_VALUE;
  return log_file;
}

/// 返回当前小时键 YYYYMMDD_HH，用于按小时切分日志文件。
/// @return 例如 "20260806_15"。
inline std::string FormatHourKey() {
  SYSTEMTIME system_time;
  ::GetLocalTime(&system_time);
  std::ostringstream stream;
  stream << std::setfill('0') << std::setw(4) << system_time.wYear
         << std::setw(2) << system_time.wMonth << std::setw(2)
         << system_time.wDay << '_' << std::setw(2) << system_time.wHour;
  return stream.str();
}

/// 诊断日志目录（UTF-8，含结尾分隔符）的进程级存储。
/// @return 目录文本的引用。
inline std::string& DiagnosticLogDirectoryState() {
  static std::string directory;
  return directory;
}

/// 诊断日志当前小时键（YYYYMMDD_HH）的进程级存储。
/// @return 小时键的引用。
inline std::string& DiagnosticLogCurrentHourState() {
  static std::string current_hour;
  return current_hour;
}

/// 诊断日志 utils LogFile 指针的进程级存储（供小时切换线程重开文件）。
/// @return LogFile 共享指针的引用。
inline utils::base::FileLogPtr& DiagnosticLogFileLogState() {
  static utils::base::FileLogPtr file_log;
  return file_log;
}

/// 初始化 utils 日志模块并在可执行文件目录创建/追加按小时切分的
/// offscreen_debug_YYYYMMDD_HH.log。日志底层由 utils::base::Logger +
/// utils::base::LogFile 实现；该函数可重复调用，内部保证只初始化一次，
/// 并启动后台线程在跨小时时自动切换到新小时文件。
inline void SetDiagnosticLogFileToApplicationDirectory() {
  wchar_t module_path[MAX_PATH] = {};
  const DWORD length =
      ::GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    return;
  }

  std::wstring log_dir(module_path, length);
  const size_t separator = log_dir.find_last_of(L"\\/");
  if (separator == std::wstring::npos) {
    return;
  }
  log_dir.resize(separator + 1);

  static std::once_flag init_flag;
  std::call_once(init_flag, [&log_dir]() {
    const std::string dir_utf8 = [&log_dir]() {
      const int path_size = ::WideCharToMultiByte(
          CP_UTF8, 0, log_dir.c_str(), static_cast<int>(log_dir.size()),
          nullptr, 0, nullptr, nullptr);
      if (path_size <= 0) {
        return std::string();
      }
      std::string utf8(path_size, '\0');
      ::WideCharToMultiByte(CP_UTF8, 0, log_dir.c_str(),
                            static_cast<int>(log_dir.size()), &utf8[0],
                            path_size, nullptr, nullptr);
      return utf8;
    }();

    DiagnosticLogDirectoryState() = dir_utf8;
    DiagnosticLogCurrentHourState() = FormatHourKey();
    DiagnosticLogFileLogState() = std::make_shared<utils::base::LogFile>();
    if (DiagnosticLogFileLogState()->Open(
            dir_utf8 + "offscreen_debug_" + FormatHourKey() + ".log")) {
      utils::base::g_logger =
          new utils::base::Logger(DiagnosticLogFileLogState());
      utils::base::g_logger->SetLogLevel(utils::base::kTrace);
    }

    // 后台线程：每 30 秒检查小时变化，跨小时自动切换到新小时文件，
    // 保证长测试期间的日志按小时切分，不混入上一小时的记录。
    std::thread([]() {
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        const std::string hour = FormatHourKey();
        if (hour != DiagnosticLogCurrentHourState() &&
            DiagnosticLogFileLogState()) {
          DiagnosticLogCurrentHourState() = hour;
          DiagnosticLogFileLogState()->Open(
              DiagnosticLogDirectoryState() + "offscreen_debug_" + hour +
              ".log");
        }
      }
    }).detach();
  });
}

/// 将地址或数值格式化为十六进制文本。
/// @param value 待格式化的无符号指针值。
/// @return 带 0x 前缀的文本。
inline std::string HexValue(uintptr_t value) {
  std::ostringstream stream;
  stream << "0x" << std::hex << value;
  return stream.str();
}

/// 返回墙钟时间戳 HH:MM:SS.mmm，供所有日志统一对时。
/// @return 例如 "12:34:56.789"。
inline std::string FormatTimestampHMSMM() {
  SYSTEMTIME system_time;
  ::GetLocalTime(&system_time);
  std::ostringstream stream;
  stream << std::setfill('0') << std::setw(2) << system_time.wHour << ':'
         << std::setw(2) << system_time.wMinute << ':'
         << std::setw(2) << system_time.wSecond << '.'
         << std::setw(3) << system_time.wMilliseconds;
  return stream.str();
}

/// 输出带进程和线程标识的诊断日志，底层写入 utils 日志模块。
/// @param message 要记录的 UTF-8 消息。
inline void DiagnosticLog(const std::string& message) {
  std::ostringstream stream;
  stream << "[" << FormatTimestampHMSMM() << "] [offscreen pid="
         << ::GetCurrentProcessId() << " tid=" << ::GetCurrentThreadId()
         << "] " << message << '\n';

  const std::string line = stream.str();
  std::lock_guard<std::mutex> lock(DiagnosticLogMutex());
  if (utils::base::g_logger != nullptr) {
    utils::base::g_logger->Write(line);
  } else {
    // utils 日志模块未初始化时的兜底路径，保证日志不丢失。
    std::clog << line;
    std::clog.flush();
  }
  ::OutputDebugStringA(line.c_str());
}

/// 按“前若干次 + 固定间隔”策略决定是否输出高频日志。
/// @param counter 调用次数计数器。
/// @param first_count 始终记录的前几次调用数量。
/// @param every_count 后续每隔多少次记录一次，非正数表示禁用间隔记录。
/// @return 本次调用是否应写入日志。
inline bool ShouldDiagnosticLog(std::atomic<int>& counter,
                                int first_count,
                                int every_count) {
  const int value = counter.fetch_add(1, std::memory_order_relaxed) + 1;
  return value <= first_count ||
         (every_count > 0 && value % every_count == 0);
}

}  // namespace offscreen
