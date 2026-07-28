#pragma once

#include <atomic>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

#include <windows.h>

namespace offscreen {

/// 返回诊断日志共享锁，串行化多线程写入。
/// @return 进程内唯一的互斥量。
inline std::mutex& DiagnosticLogMutex() {
  static std::mutex log_mutex;
  return log_mutex;
}

/// 返回当前诊断日志文件句柄的存储位置。
/// @return 可读写的进程级文件句柄引用。
inline HANDLE& DiagnosticLogFileHandle() {
  static HANDLE log_file = INVALID_HANDLE_VALUE;
  return log_file;
}

/// 在可执行文件目录创建或追加诊断日志文件。
inline void SetDiagnosticLogFileToApplicationDirectory() {
  wchar_t module_path[MAX_PATH] = {};
  const DWORD length =
      ::GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    return;
  }

  std::wstring log_path(module_path, length);
  const size_t separator = log_path.find_last_of(L"\\\\/");
  if (separator == std::wstring::npos) {
    return;
  }
  log_path.resize(separator + 1);
  log_path += L"offscreen_debug.log";

  const HANDLE log_file = ::CreateFileW(
      log_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (log_file == INVALID_HANDLE_VALUE) {
    return;
  }

  std::lock_guard<std::mutex> lock(DiagnosticLogMutex());
  HANDLE& current_log_file = DiagnosticLogFileHandle();
  if (current_log_file != INVALID_HANDLE_VALUE) {
    ::CloseHandle(current_log_file);
  }
  current_log_file = log_file;
}

/// 将地址或数值格式化为十六进制文本。
/// @param value 待格式化的无符号指针值。
/// @return 带 0x 前缀的文本。
inline std::string HexValue(uintptr_t value) {
  std::ostringstream stream;
  stream << "0x" << std::hex << value;
  return stream.str();
}

/// 输出带进程和线程标识的诊断日志。
/// @param message 要记录的 UTF-8 消息。
inline void DiagnosticLog(const std::string& message) {
  std::ostringstream stream;
  stream << "[offscreen pid=" << ::GetCurrentProcessId()
         << " tid=" << ::GetCurrentThreadId() << "] " << message << '\n';

  const std::string line = stream.str();
  std::lock_guard<std::mutex> lock(DiagnosticLogMutex());
  std::clog << line;
  std::clog.flush();
  ::OutputDebugStringA(line.c_str());

  const HANDLE log_file = DiagnosticLogFileHandle();
  if (log_file != INVALID_HANDLE_VALUE) {
    DWORD bytes_written = 0;
    ::WriteFile(log_file, line.data(), static_cast<DWORD>(line.size()),
                &bytes_written, nullptr);
  }
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
