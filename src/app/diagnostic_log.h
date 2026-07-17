#pragma once

#include <atomic>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

#include <windows.h>

namespace offscreen {

inline std::mutex& DiagnosticLogMutex() {
  static std::mutex log_mutex;
  return log_mutex;
}

inline HANDLE& DiagnosticLogFileHandle() {
  static HANDLE log_file = INVALID_HANDLE_VALUE;
  return log_file;
}

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

inline std::string HexValue(uintptr_t value) {
  std::ostringstream stream;
  stream << "0x" << std::hex << value;
  return stream.str();
}

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

inline bool ShouldDiagnosticLog(std::atomic<int>& counter,
                                int first_count,
                                int every_count) {
  const int value = counter.fetch_add(1, std::memory_order_relaxed) + 1;
  return value <= first_count ||
         (every_count > 0 && value % every_count == 0);
}

}  // namespace offscreen
