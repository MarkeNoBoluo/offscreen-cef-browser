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

/// 当前日志文件对应的小时键（YYYYMMDD_HH），用于跨小时切换。
std::string& OsrRenderLogCurrentHour() {
  static std::string current_hour;
  return current_hour;
}

/// 日志文件所在目录（UTF-8），供跨小时切换时构造新路径。
std::string& OsrRenderLogDirectory() {
  static std::string directory;
  return directory;
}

/// 将 UTF-8 字符串转换为宽字符字符串。
/// @param utf8 输入 UTF-8 文本。
/// @return 宽字符文本；转换失败时为空。
std::wstring Utf8ToWideString(const std::string& utf8) {
  if (utf8.empty()) {
    return std::wstring();
  }
  const int length = ::MultiByteToWideChar(
      CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  if (length <= 0) {
    return std::wstring();
  }
  std::wstring wide(length, L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                        static_cast<int>(utf8.size()), &wide[0], length);
  return wide;
}

/// 将宽字符字符串转换为 UTF-8 字符串。
/// @param wide 输入宽字符文本。
/// @return UTF-8 文本；转换失败时为空。
std::string WideToUtf8String(const std::wstring& wide) {
  if (wide.empty()) {
    return std::string();
  }
  const int length = ::WideCharToMultiByte(
      CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
      nullptr, nullptr);
  if (length <= 0) {
    return std::string();
  }
  std::string utf8(static_cast<std::size_t>(length), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                        static_cast<int>(wide.size()), utf8.data(), length,
                        nullptr, nullptr);
  return utf8;
}

/// 将窄字符串按 ASCII 直接转为宽字符（用于小时键等纯 ASCII 文本）。
/// @param value 输入窄字符串。
/// @return 宽字符字符串。
std::wstring StringToWide(const std::string& value) {
  std::wstring wide(value.begin(), value.end());
  return wide;
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

/// 以追加模式打开 CSV 文件；文件为空时先写 UTF-8 BOM + 表头。
/// 调用方需已持有 OsrRenderLogMutex。
/// @param path 目标文件完整路径（宽字符）。
/// @return 打开的句柄；失败返回 INVALID_HANDLE_VALUE。
HANDLE OpenOsrRenderLogFileLocked(const std::wstring& path) {
  const HANDLE log_file = ::CreateFileW(
      path.c_str(), FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (log_file == INVALID_HANDLE_VALUE) {
    return INVALID_HANDLE_VALUE;
  }

  const DWORD file_size = ::GetFileSize(log_file, nullptr);
  if (file_size == 0) {
    static const char kUtf8Bom[] = "\xEF\xBB\xBF";
    DWORD bytes_written = 0;
    ::WriteFile(log_file, kUtf8Bom, 3, &bytes_written, nullptr);
    const std::string header = FormatOsrRenderCsvHeader() + "\n";
    ::WriteFile(log_file, header.data(), static_cast<DWORD>(header.size()),
                &bytes_written, nullptr);
  }
  return log_file;
}

void SetOsrRenderLogFile(const std::wstring& path) {
  std::lock_guard<std::mutex> lock(OsrRenderLogMutex());
  HANDLE& current_log_file = OsrRenderLogFileHandle();
  if (current_log_file != INVALID_HANDLE_VALUE) {
    ::CloseHandle(current_log_file);
    current_log_file = INVALID_HANDLE_VALUE;
  }
  current_log_file = OpenOsrRenderLogFileLocked(path);
}

void SetOsrRenderLogFileToApplicationDirectory() {
  wchar_t module_path[MAX_PATH] = {};
  const DWORD length = ::GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    return;
  }
  std::wstring log_dir(module_path, length);
  const size_t separator = log_dir.find_last_of(L"\\/");
  if (separator == std::wstring::npos) {
    return;
  }
  log_dir.resize(separator + 1);
  const std::wstring log_path =
      log_dir + L"offscreen_osr_render_" + StringToWide(FormatHourKey()) +
      L".csv";

  std::lock_guard<std::mutex> lock(OsrRenderLogMutex());
  HANDLE& current_log_file = OsrRenderLogFileHandle();
  if (current_log_file != INVALID_HANDLE_VALUE) {
    ::CloseHandle(current_log_file);
    current_log_file = INVALID_HANDLE_VALUE;
  }
  current_log_file = OpenOsrRenderLogFileLocked(log_path);
  OsrRenderLogCurrentHour() = FormatHourKey();
  OsrRenderLogDirectory() = WideToUtf8String(log_dir);
}

void RotateOsrRenderLogIfHourChanged() {
  std::lock_guard<std::mutex> lock(OsrRenderLogMutex());
  const std::string hour = FormatHourKey();
  if (hour == OsrRenderLogCurrentHour() || OsrRenderLogDirectory().empty()) {
    return;
  }
  HANDLE& current_log_file = OsrRenderLogFileHandle();
  if (current_log_file != INVALID_HANDLE_VALUE) {
    ::CloseHandle(current_log_file);
    current_log_file = INVALID_HANDLE_VALUE;
  }
  const std::wstring log_path =
      Utf8ToWideString(OsrRenderLogDirectory() + "offscreen_osr_render_" +
                       hour + ".csv");
  current_log_file = OpenOsrRenderLogFileLocked(log_path);
  OsrRenderLogCurrentHour() = hour;
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
