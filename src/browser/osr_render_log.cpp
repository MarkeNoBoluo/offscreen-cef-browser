#include "browser/osr_render_log.h"

#include <mutex>
#include <sstream>
#include <string>

#include <windows.h>

#include "app/diagnostic_log.h"

namespace offscreen {

namespace {

std::mutex& OsrRenderLogMutex() {
  static std::mutex log_mutex;
  return log_mutex;
}

HANDLE& OsrRenderLogFileHandle() {
  static HANDLE log_file = INVALID_HANDLE_VALUE;
  return log_file;
}

/// 对单个 CSV 字段做转义：含逗号/引号/换行时双引号包裹、内部引号翻倍。
/// @param value 原始字段文本。
/// @return 可直接写入 CSV 的字段文本。
std::string CsvField(const std::string& value) {
  const bool needs_quote = value.find(',') != std::string::npos ||
                           value.find('"') != std::string::npos ||
                           value.find('\n') != std::string::npos ||
                           value.find('\r') != std::string::npos;
  if (!needs_quote) {
    return value;
  }
  std::string escaped;
  escaped.reserve(value.size() + 2);
  escaped += '"';
  for (const char c : value) {
    if (c == '"') {
      escaped += '"';
    }
    escaped += c;
  }
  escaped += '"';
  return escaped;
}

}  // namespace

void SetOsrRenderLogFile(const std::wstring& path) {
  const HANDLE log_file = ::CreateFileW(
      path.c_str(), FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (log_file == INVALID_HANDLE_VALUE) {
    return;
  }

  std::lock_guard<std::mutex> lock(OsrRenderLogMutex());
  HANDLE& current_log_file = OsrRenderLogFileHandle();
  if (current_log_file != INVALID_HANDLE_VALUE) {
    ::CloseHandle(current_log_file);
  }
  current_log_file = log_file;

  const DWORD file_size = ::GetFileSize(log_file, nullptr);
  if (file_size == 0) {
    static const char kUtf8Bom[] = "\xEF\xBB\xBF";
    DWORD bytes_written = 0;
    ::WriteFile(log_file, kUtf8Bom, 3, &bytes_written, nullptr);
    const std::string header = FormatOsrRenderCsvHeader() + "\n";
    ::WriteFile(log_file, header.data(), static_cast<DWORD>(header.size()),
                &bytes_written, nullptr);
  }
}

void SetOsrRenderLogFileToApplicationDirectory() {
  wchar_t module_path[MAX_PATH] = {};
  const DWORD length = ::GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    return;
  }
  std::wstring log_path(module_path, length);
  const size_t separator = log_path.find_last_of(L"\\\\/");
  if (separator == std::wstring::npos) {
    return;
  }
  log_path.resize(separator + 1);
  log_path += L"offscreen_osr_render.csv";
  SetOsrRenderLogFile(log_path);
}

void OsrRenderLogWrite(const OsrRenderLogRecord& record) {
  const std::string line = FormatOsrRenderCsvRow(record);
  std::lock_guard<std::mutex> lock(OsrRenderLogMutex());
  const HANDLE log_file = OsrRenderLogFileHandle();
  if (log_file != INVALID_HANDLE_VALUE) {
    DWORD bytes_written = 0;
    ::WriteFile(log_file, line.data(), static_cast<DWORD>(line.size()),
                &bytes_written, nullptr);
  }
}

std::string FormatOsrRenderCsvHeader() {
  return "timestamp,event,browser_id,x,y,w,h,scale,type,show,dirty_count,"
         "dirty_area_px,detail";
}

std::string FormatOsrRenderCsvRow(const OsrRenderLogRecord& record) {
  std::ostringstream stream;
  stream << FormatTimestampHMSMM() << ',' << CsvField(record.event) << ','
         << record.browser_id << ',' << record.x << ',' << record.y << ','
         << record.w << ',' << record.h << ',' << record.scale << ','
         << CsvField(record.type) << ',' << CsvField(record.show) << ','
         << record.dirty_count << ',' << record.dirty_area_px << ','
         << CsvField(record.detail) << '\n';
  return stream.str();
}

}  // namespace offscreen
