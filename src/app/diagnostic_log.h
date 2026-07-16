#pragma once

#include <atomic>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

#include <windows.h>

namespace offscreen {

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
  static std::mutex log_mutex;
  std::lock_guard<std::mutex> lock(log_mutex);
  std::clog << line;
  std::clog.flush();
  ::OutputDebugStringA(line.c_str());
}

inline bool ShouldDiagnosticLog(std::atomic<int>& counter,
                                int first_count,
                                int every_count) {
  const int value = counter.fetch_add(1, std::memory_order_relaxed) + 1;
  return value <= first_count ||
         (every_count > 0 && value % every_count == 0);
}

}  // namespace offscreen
