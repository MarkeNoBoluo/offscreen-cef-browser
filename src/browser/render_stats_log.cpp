#include "browser/render_stats_log.h"

#include <chrono>
#include <cwchar>
#include <cwctype>
#include <mutex>
#include <sstream>
#include <string>

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include "app/diagnostic_log.h"

namespace offscreen {

namespace {

std::mutex& RenderStatsLogMutex() {
  static std::mutex log_mutex;
  return log_mutex;
}

HANDLE& RenderStatsLogFileHandle() {
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

/// 将 FILETIME 转换为 100ns 单位的浮点计数值。
/// @param ft 输入时间戳。
/// @return 100ns 计数。
double FileTimeToTicks(const FILETIME& ft) {
  ULARGE_INTEGER value;
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  return static_cast<double>(value.QuadPart);
}

/// 进程 CPU 采样基线：首次调用只记录起点，之后返回上一采样间隔的增量占用率。
struct CpuBaseline {
  FILETIME kernel_time;
  FILETIME user_time;
  std::chrono::steady_clock::time_point wall;
  bool valid = false;
};

CpuBaseline& CpuBaselineState() {
  static CpuBaseline baseline;
  return baseline;
}

/// 采样进程工作集（字节）。
/// @param working_set_bytes 输出工作集字节数。
/// @return 是否采样成功。
bool SampleWorkingSetBytes(uint64_t* working_set_bytes) {
  PROCESS_MEMORY_COUNTERS counters = {};
  if (!::GetProcessMemoryInfo(::GetCurrentProcess(), &counters,
                              sizeof(counters))) {
    return false;
  }
  *working_set_bytes = counters.WorkingSetSize;
  return true;
}

/// 将宽字符转换为 UTF-8 字符串。
/// @param wide 输入宽字符字符串。
/// @return UTF-8 编码文本。
std::string WideToUtf8(const std::wstring& wide) {
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

/// 从进程命令行提取 --use-angle / --use-gl 参数值；未指定返回 "default"。
/// @param command_line 原始命令行。
/// @return 渲染后端参数值。
std::string ExtractGpuBackend(const std::wstring& command_line) {
  static const wchar_t* const kAnglePrefix = L"--use-angle=";
  static const wchar_t* const kGlPrefix = L"--use-gl=";
  const std::wstring::size_type angle_pos = command_line.find(kAnglePrefix);
  if (angle_pos != std::wstring::npos) {
    const std::wstring::size_type value_begin = angle_pos + wcslen(kAnglePrefix);
    const std::wstring::size_type value_end = command_line.find(
        L' ', value_begin);
    return "angle/" + WideToUtf8(command_line.substr(
                          value_begin, value_end == std::wstring::npos
                                            ? std::wstring::npos
                                            : value_end - value_begin));
  }
  const std::wstring::size_type gl_pos = command_line.find(kGlPrefix);
  if (gl_pos != std::wstring::npos) {
    const std::wstring::size_type value_begin = gl_pos + wcslen(kGlPrefix);
    const std::wstring::size_type value_end =
        command_line.find(L' ', value_begin);
    return "gl/" + WideToUtf8(command_line.substr(
                       value_begin, value_end == std::wstring::npos
                                         ? std::wstring::npos
                                         : value_end - value_begin));
  }
  return "default";
}

/// 解析命令行并缓存 GPU 开关结果（启动后命令行不再变化）。
/// @return 缓存的命令行 GPU 信息。
const GpuBackendInfo& CachedCommandLineGpuInfo() {
  static const GpuBackendInfo cached = [] {
    GpuBackendInfo info;
    const std::wstring command_line(::GetCommandLineW());
    info.gpu_requested =
        command_line.find(L"--disable-gpu") == std::wstring::npos;
    info.backend = ExtractGpuBackend(command_line);
    return info;
  }();
  return cached;
}

/// 当前可执行文件的基名（不含扩展名），用于识别 CEF 子进程。
/// @return 进程基名（小写）。
std::wstring CurrentProcessBaseNameLower() {
  wchar_t module_path[MAX_PATH] = {};
  const DWORD length = ::GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    return std::wstring();
  }
  std::wstring path(module_path, length);
  const std::wstring::size_type separator = path.find_last_of(L"\\/");
  if (separator != std::wstring::npos) {
    path = path.substr(separator + 1);
  }
  for (wchar_t& c : path) {
    c = static_cast<wchar_t>(::towlower(c));
  }
  return path;
}

}  // namespace

/// 采样 GPU 环境信息：命令行开关 + 存活 CEF 子进程计数。
/// gpu_requested/backend 来自启动命令行（进程生命周期内不变，缓存）；
/// subprocess_count 每次实时枚举：匹配当前可执行文件基名与
/// offscreen_cef_subprocess.exe 的进程数（含 gpu/renderer/utility 进程）。
GpuBackendInfo SampleGpuBackendInfo() {
  GpuBackendInfo info = CachedCommandLineGpuInfo();

  const std::wstring current_base = CurrentProcessBaseNameLower();
  const std::wstring cef_subprocess_base = L"offscreen_cef_subprocess.exe";

  HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return info;
  }
  PROCESSENTRY32W entry = {};
  entry.dwSize = sizeof(entry);
  for (BOOL ok = ::Process32FirstW(snapshot, &entry); ok;
       ok = ::Process32NextW(snapshot, &entry)) {
    std::wstring name(entry.szExeFile);
    for (wchar_t& c : name) {
      c = static_cast<wchar_t>(::towlower(c));
    }
    if (entry.th32ProcessID != ::GetCurrentProcessId() &&
        (name == current_base || name == cef_subprocess_base)) {
      ++info.subprocess_count;
    }
  }
  ::CloseHandle(snapshot);
  return info;
}

/// 采样进程 CPU 占用率。首次调用仅建立基线并返回 false（不产出百分比），
/// 否则按进程寿命累计算出的首行会是假的大值；此后返回自上次采样以来的
/// 增量占用率（多核下可超过 100%）。
/// @param cpu_percent 输出 CPU 百分比。
/// @return 本次是否产出了有效百分比。
bool SampleCpuPercent(double* cpu_percent) {
  CpuBaseline& baseline = CpuBaselineState();
  FILETIME create_time;
  FILETIME exit_time;
  FILETIME kernel_time;
  FILETIME user_time;
  if (!::GetProcessTimes(::GetCurrentProcess(), &create_time, &exit_time,
                         &kernel_time, &user_time)) {
    return false;
  }
  const auto now = std::chrono::steady_clock::now();
  if (!baseline.valid) {
    baseline.kernel_time = kernel_time;
    baseline.user_time = user_time;
    baseline.wall = now;
    baseline.valid = true;
    return false;
  }

  const double kernel_delta =
      FileTimeToTicks(kernel_time) - FileTimeToTicks(baseline.kernel_time);
  const double user_delta =
      FileTimeToTicks(user_time) - FileTimeToTicks(baseline.user_time);
  // 先用旧基线算墙钟间隔，之后才更新基线；否则 (now - baseline.wall) 恒为 0。
  const double wall_seconds =
      std::chrono::duration<double>(now - baseline.wall).count();
  if (wall_seconds <= 0.0) {
    return false;
  }
  baseline.kernel_time = kernel_time;
  baseline.user_time = user_time;
  baseline.wall = now;
  // FILETIME 以 100ns 为单位；cpu 时间秒 = 计数 * 1e-7。
  const double cpu_seconds = (kernel_delta + user_delta) * 100.0e-9;
  *cpu_percent = cpu_seconds / wall_seconds * 100.0;
  return true;
}

void SetRenderStatsLogFile(const std::wstring& path) {
  const HANDLE log_file = ::CreateFileW(
      path.c_str(), FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (log_file == INVALID_HANDLE_VALUE) {
    return;
  }

  std::lock_guard<std::mutex> lock(RenderStatsLogMutex());
  HANDLE& current_log_file = RenderStatsLogFileHandle();
  if (current_log_file != INVALID_HANDLE_VALUE) {
    ::CloseHandle(current_log_file);
  }
  current_log_file = log_file;

  const DWORD file_size = ::GetFileSize(log_file, nullptr);
  if (file_size == 0) {
    static const char kUtf8Bom[] = "\xEF\xBB\xBF";
    DWORD bytes_written = 0;
    ::WriteFile(log_file, kUtf8Bom, 3, &bytes_written, nullptr);
    const std::string header = FormatRenderStatsCsvHeader() + "\n";
    ::WriteFile(log_file, header.data(), static_cast<DWORD>(header.size()),
                &bytes_written, nullptr);
  }
}

void SetRenderStatsLogFileToApplicationDirectory() {
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
  log_path += L"offscreen_render_stats.csv";
  SetRenderStatsLogFile(log_path);
}

void RenderStatsLogWrite(const RenderStatsSnapshot& snapshot) {
  std::lock_guard<std::mutex> lock(RenderStatsLogMutex());
  const HANDLE log_file = RenderStatsLogFileHandle();
  if (log_file == INVALID_HANDLE_VALUE) {
    return;
  }

  double cpu_percent = 0.0;
  SampleCpuPercent(&cpu_percent);
  uint64_t working_set_bytes = 0;
  SampleWorkingSetBytes(&working_set_bytes);
  const GpuBackendInfo gpu_info = SampleGpuBackendInfo();

  const std::string line = FormatRenderStatsCsvRow(snapshot, cpu_percent,
                                                   working_set_bytes, gpu_info);
  DWORD bytes_written = 0;
  ::WriteFile(log_file, line.data(), static_cast<DWORD>(line.size()),
              &bytes_written, nullptr);
}

std::string FormatRenderStatsCsvHeader() {
  return "timestamp,elapsed_ms,cef_paint_fps,qt_paint_fps,window_frames,"
         "window_paint_events,accelerated_frames,dropped_frames,"
         "onpaint_avg_ms,set_view_image_avg_ms,schedule_delay_avg_ms,"
         "snapshot_avg_ms,draw_image_avg_ms,"
         "frame_interval_avg_ms,dirty_area_avg_px,dirty_count_avg,"
         "buffer_width,buffer_height,buffer_bytes,cpu_percent,working_set_mb,"
         "gpu_requested,gpu_backend,cef_subprocess_count";
}

std::string FormatRenderStatsCsvRow(const RenderStatsSnapshot& snapshot,
                                    double cpu_percent,
                                    uint64_t working_set_bytes) {
  return FormatRenderStatsCsvRow(snapshot, cpu_percent, working_set_bytes,
                                 SampleGpuBackendInfo());
}

std::string FormatRenderStatsCsvRow(const RenderStatsSnapshot& snapshot,
                                    double cpu_percent,
                                    uint64_t working_set_bytes,
                                    const GpuBackendInfo& gpu_info) {
  constexpr std::size_t kStageCount =
      static_cast<std::size_t>(RenderStage::kCount);
  const double dirty_frames = static_cast<double>(snapshot.dirty_frames);

  std::ostringstream stream;
  stream << CsvField(FormatTimestampHMSMM()) << ','
         << static_cast<int64_t>(snapshot.window_seconds * 1000.0) << ','
         << snapshot.fps << ',' << snapshot.qt_fps << ','
         << snapshot.window_frames << ',' << snapshot.window_paint_events << ','
         << snapshot.window_accelerated_frames << ','
         << snapshot.window_dropped_frames << ',';
  for (std::size_t i = 0; i < kStageCount; ++i) {
    stream << snapshot.stages[i].avg_ms() << ',';
  }
  stream << snapshot.frame_interval_ms.avg_ms() << ','
         << (dirty_frames > 0 ? snapshot.dirty_area_sum_px / dirty_frames : 0.0)
         << ','
         << (dirty_frames > 0 ? snapshot.dirty_rect_count_sum / dirty_frames
                              : 0.0)
         << ',' << snapshot.last_width << ',' << snapshot.last_height << ','
         << static_cast<int64_t>(snapshot.last_width) * snapshot.last_height * 4
         << ',' << cpu_percent << ','
         << static_cast<double>(working_set_bytes) / 1048576.0 << ','
         << (gpu_info.gpu_requested ? 1 : 0) << ','
         << CsvField(gpu_info.backend) << ',' << gpu_info.subprocess_count
         << '\n';
  return stream.str();
}

}  // namespace offscreen
