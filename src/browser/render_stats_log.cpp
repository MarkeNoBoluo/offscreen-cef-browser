#include "browser/render_stats_log.h"

#include <chrono>
#include <cctype>
#include <cwchar>
#include <cwctype>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <winternl.h>

#include "app/diagnostic_log.h"
#include "browser/run_lifecycle.h"

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

/// 当前日志文件对应的小时键（YYYYMMDD_HH），用于跨小时切换。
std::string& RenderStatsLogCurrentHour() {
  static std::string current_hour;
  return current_hour;
}

/// 日志文件所在目录（UTF-8），供跨小时切换时构造新路径。
std::string& RenderStatsLogDirectory() {
  static std::string directory;
  return directory;
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

/// 进程 CPU 采样基线（每个 pid 一份）：进程退出后条目会自然清理。
struct ProcessCpuBaseline {
  FILETIME kernel_time;
  FILETIME user_time;
  std::chrono::steady_clock::time_point wall;
  bool valid = false;
};

/// 按 pid 缓存各进程的 CPU 采样基线。
std::unordered_map<DWORD, ProcessCpuBaseline>& ProcessCpuBaselines() {
  static std::unordered_map<DWORD, ProcessCpuBaseline> baselines;
  return baselines;
}

/// 采样单进程 CPU 占用率（增量法）。首次调用仅建立基线并返回 false。
/// @param pid 目标进程 id；0 表示当前进程。
/// @param now 当前墙钟时刻。
/// @param baselines 基线缓存（进程退出后条目被清理）。
/// @param cpu_percent 输出 CPU 百分比（多核可超 100）。
/// @return 本次是否产出了有效百分比。
bool SamplePidCpuPercent(DWORD pid,
                         const std::chrono::steady_clock::time_point& now,
                         std::unordered_map<DWORD, ProcessCpuBaseline>& baselines,
                         double* cpu_percent) {
  if (pid == 0) {
    pid = ::GetCurrentProcessId();
  }
  const HANDLE process = ::OpenProcess(
      PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (process == nullptr) {
    baselines.erase(pid);  // 进程已退出，清理基线避免 pid 复用污染。
    return false;
  }
  FILETIME create_time;
  FILETIME exit_time;
  FILETIME kernel_time;
  FILETIME user_time;
  const BOOL ok = ::GetProcessTimes(process, &create_time, &exit_time,
                                    &kernel_time, &user_time);
  ::CloseHandle(process);
  if (!ok) {
    baselines.erase(pid);
    return false;
  }

  auto& baseline = baselines[pid];
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
  const double wall_seconds =
      std::chrono::duration<double>(now - baseline.wall).count();
  baseline.kernel_time = kernel_time;
  baseline.user_time = user_time;
  baseline.wall = now;
  if (wall_seconds <= 0.0) {
    return false;
  }
  // FILETIME 以 100ns 为单位；cpu 时间秒 = 计数 * 1e-7。
  const double cpu_seconds = (kernel_delta + user_delta) * 100.0e-9;
  *cpu_percent = cpu_seconds / wall_seconds * 100.0;
  return true;
}

/// 采样单进程工作集（字节）；失败返回 0。
/// @param pid 目标进程 id；0 表示当前进程。
/// @return 工作集字节数。
uint64_t SamplePidWorkingSet(DWORD pid) {
  if (pid == 0) {
    pid = ::GetCurrentProcessId();
  }
  const HANDLE process = ::OpenProcess(
      PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (process == nullptr) {
    return 0;
  }
  PROCESS_MEMORY_COUNTERS counters = {};
  const BOOL ok = ::GetProcessMemoryInfo(process, &counters, sizeof(counters));
  ::CloseHandle(process);
  return ok ? counters.WorkingSetSize : 0;
}

/// 采样宿主进程工作集（字节）。
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

/// 将窄字符串按 ASCII 直接转为宽字符（用于小时键等纯 ASCII 文本）。
/// @param value 输入窄字符串。
/// @return 宽字符字符串。
std::wstring StringToWide(const std::string& value) {
  return std::wstring(value.begin(), value.end());
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

/// 读取指定进程的命令行（通过 PEB 的 ProcessParameters）。
/// x64 专用（项目强制 x64）：PEB+0x20 = ProcessParameters，
/// RTL_USER_PROCESS_PARAMETERS+0x70 = CommandLine（UNICODE_STRING）。
/// @param pid 目标进程 id。
/// @return 进程命令行；读取失败时为空。
std::wstring ReadProcessCommandLine(DWORD pid) {
  const HANDLE process = ::OpenProcess(
      PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
  if (process == nullptr) {
    return std::wstring();
  }

  // ProcessBasicInformation → PEB 地址。
  struct ProcessBasicInformation {
    PVOID reserved1;
    PVOID peb_base_address;
    PVOID reserved2[2];
    ULONG_PTR unique_process_id;
    PVOID reserved3;
  };
  ProcessBasicInformation basic = {};
  ULONG return_length = 0;
  typedef NTSTATUS(NTAPI* NtQueryInformationProcessFn)(
      HANDLE, ULONG, PVOID, ULONG, PULONG);
  const NtQueryInformationProcessFn query =
      reinterpret_cast<NtQueryInformationProcessFn>(
          ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"),
                           "NtQueryInformationProcess"));
  if (query == nullptr ||
      query(process, 0 /*ProcessBasicInformation*/, &basic, sizeof(basic),
            &return_length) != 0 ||
      basic.peb_base_address == nullptr) {
    ::CloseHandle(process);
    return std::wstring();
  }

  // PEB.ProcessParameters（x64 偏移 0x20）。
  BYTE* process_parameters = nullptr;
  SIZE_T bytes_read = 0;
  if (!::ReadProcessMemory(
          process, reinterpret_cast<BYTE*>(basic.peb_base_address) + 0x20,
          &process_parameters, sizeof(process_parameters), &bytes_read)) {
    ::CloseHandle(process);
    return std::wstring();
  }
  if (process_parameters == nullptr) {
    ::CloseHandle(process);
    return std::wstring();
  }

  // RTL_USER_PROCESS_PARAMETERS.CommandLine（x64 偏移 0x70）：
  // UNICODE_STRING { USHORT Length; USHORT MaximumLength; PWSTR Buffer; }。
  struct UnicodeString {
    USHORT length;
    USHORT maximum_length;
    wchar_t* buffer;
  };
  UnicodeString command_line = {};
  if (!::ReadProcessMemory(process,
                           reinterpret_cast<BYTE*>(process_parameters) + 0x70,
                           &command_line, sizeof(command_line), &bytes_read) ||
      command_line.buffer == nullptr || command_line.length == 0) {
    ::CloseHandle(process);
    return std::wstring();
  }
  const std::size_t char_count = command_line.length / sizeof(wchar_t);
  std::wstring result;
  result.resize(char_count);
  if (!::ReadProcessMemory(process, command_line.buffer, &result[0],
                           command_line.length, &bytes_read)) {
    result.clear();
  }
  ::CloseHandle(process);
  return result;
}

/// cef.log 扫描器：增量读取新增内容，统计 GPU 初始化/GL 错误并推断实际后端。
struct CefLogScanner {
  HANDLE file = INVALID_HANDLE_VALUE;
  DWORD last_size = 0;
  bool opened_once = false;
  int error_count = 0;
  bool gl_disabled = false;
};

/// 全局 cef.log 扫描器状态。
CefLogScanner& CefLogScannerState() {
  static CefLogScanner scanner;
  return scanner;
}

/// 检查文本是否包含任一错误特征串（GPU 初始化/GL 禁用/致命错误）。
/// @param text 待检查文本（UTF-8）。
/// @param gl_disabled 输出：是否检测到 GL 禁用。
/// @return 是否为 GPU 初始化错误行。
bool IsGpuInitErrorLine(const std::string& text, bool* gl_disabled) {
  static const char* const kErrorPatterns[] = {
      "gpu_init.cc", "Passthrough is not supported", "GL is disabled",
      "GPU process launch failed", "Failed to create GL context",
      "FATAL:gpu", "ERROR:gpu"};
  static const char* const kGlDisabledPatterns[] = {
      "GL is disabled", "Passthrough is not supported", "gpu_init.cc"};
  bool matched = false;
  for (const char* pattern : kErrorPatterns) {
    if (text.find(pattern) != std::string::npos) {
      matched = true;
      break;
    }
  }
  for (const char* pattern : kGlDisabledPatterns) {
    if (text.find(pattern) != std::string::npos) {
      *gl_disabled = true;
      break;
    }
  }
  return matched;
}

/// 扫描 cef.log 新增内容并累计 GPU 初始化错误数。
/// 首次打开时全量扫描已有内容（GPU 初始化错误通常在启动早期写入），
/// 之后只扫描增量，避免重复计数。
/// @param scanner 扫描器状态。
void ScanCefLog(CefLogScanner& scanner) {
  // cef.log 位于可执行文件目录。
  if (!scanner.opened_once) {
    scanner.opened_once = true;
    wchar_t module_path[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
      return;
    }
    std::wstring log_path(module_path, length);
    const size_t separator = log_path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
      return;
    }
    log_path.resize(separator + 1);
    log_path += L"cef.log";
    scanner.file = ::CreateFileW(
        log_path.c_str(), FILE_READ_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (scanner.file == INVALID_HANDLE_VALUE) {
      return;
    }
    scanner.last_size = 0;  // 从 0 开始：首次打开即全量扫描已有内容。
  }
  if (scanner.file == INVALID_HANDLE_VALUE) {
    return;
  }
  const DWORD size = ::GetFileSize(scanner.file, nullptr);
  if (size <= scanner.last_size) {
    return;  // 无新增内容。
  }
  const DWORD chunk_size = size - scanner.last_size;
  std::string chunk(static_cast<std::size_t>(chunk_size), '\0');
  ::SetFilePointer(scanner.file, scanner.last_size, nullptr, FILE_BEGIN);
  DWORD bytes_read = 0;
  ::ReadFile(scanner.file, &chunk[0], chunk_size, &bytes_read, nullptr);
  scanner.last_size = size;

  bool gl_disabled = false;
  if (IsGpuInitErrorLine(chunk, &gl_disabled)) {
    // 一个增量块可能含多行错误；按换行切分逐行统计。
    std::size_t pos = 0;
    while (pos < chunk.size()) {
      const std::size_t newline = chunk.find('\n', pos);
      const std::string line =
          chunk.substr(pos, newline == std::string::npos
                                ? std::string::npos
                                : newline - pos);
      bool line_gl_disabled = false;
      if (IsGpuInitErrorLine(line, &line_gl_disabled)) {
        ++scanner.error_count;
      }
      if (line_gl_disabled) {
        gl_disabled = true;
      }
      if (newline == std::string::npos) {
        break;
      }
      pos = newline + 1;
    }
  }
  if (gl_disabled) {
    scanner.gl_disabled = true;
  }
}

/// GPU 进程 pid 集合追踪：用于统计 GPU 进程重启次数。
struct GpuPidTracker {
  std::unordered_set<DWORD> pids;
  bool initialized = false;
  int restart_count = 0;
};

/// 全局 GPU 进程追踪状态。
GpuPidTracker& GpuPidTrackerState() {
  static GpuPidTracker tracker;
  return tracker;
}

/// 以追加模式打开 CSV 文件；文件为空时先写 UTF-8 BOM + 表头。
/// 调用方需已持有 RenderStatsLogMutex。
/// @param path 目标文件完整路径（宽字符）。
/// @return 打开的句柄；失败返回 INVALID_HANDLE_VALUE。
HANDLE OpenRenderStatsLogFileLocked(const std::wstring& path) {
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
    const std::string header = FormatRenderStatsCsvHeader() + "\n";
    ::WriteFile(log_file, header.data(), static_cast<DWORD>(header.size()),
                &bytes_written, nullptr);
  }
  return log_file;
}

}  // namespace

/// 采样 GPU 环境信息：命令行开关 + 存活 CEF 子进程计数 + cef.log 实际状态。
/// gpu_requested/backend 来自启动命令行（进程生命周期内不变，缓存）；
/// subprocess_count 每次实时枚举：匹配当前可执行文件基名与
/// offscreen_cef_subprocess.exe 的进程数（含 gpu/renderer/utility 进程）；
/// gpu_init_error_count/gpu_actual_backend 来自 cef.log 增量扫描；
/// gpu_process_restarted_count 由 GPU 子进程 pid 集合变化累计。
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
  std::unordered_set<DWORD> gpu_pids_now;
  for (BOOL ok = ::Process32FirstW(snapshot, &entry); ok;
       ok = ::Process32NextW(snapshot, &entry)) {
    std::wstring name(entry.szExeFile);
    for (wchar_t& c : name) {
      c = static_cast<wchar_t>(::towlower(c));
    }
    if (entry.th32ProcessID == ::GetCurrentProcessId()) {
      continue;
    }
    if (name != current_base && name != cef_subprocess_base) {
      continue;
    }
    ++info.subprocess_count;
    // 识别 GPU 进程：命令行包含 --type=gpu-process。
    const std::wstring command_line = ReadProcessCommandLine(
        entry.th32ProcessID);
    if (command_line.find(L"--type=gpu-process") != std::wstring::npos) {
      gpu_pids_now.insert(entry.th32ProcessID);
    }
  }
  ::CloseHandle(snapshot);

  // GPU 进程重启计数：上次快照中消失的 pid 计为一次重启。
  GpuPidTracker& tracker = GpuPidTrackerState();
  if (tracker.initialized) {
    for (DWORD old_pid : tracker.pids) {
      if (gpu_pids_now.find(old_pid) == gpu_pids_now.end()) {
        ++tracker.restart_count;
      }
    }
  } else {
    tracker.initialized = true;
  }
  tracker.pids = std::move(gpu_pids_now);
  info.gpu_process_restarted_count = tracker.restart_count;

  // cef.log 增量扫描：累计 GPU 初始化错误并推断实际后端。
  CefLogScanner& scanner = CefLogScannerState();
  ScanCefLog(scanner);
  info.gpu_init_error_count = scanner.error_count;
  info.actual_backend =
      scanner.gl_disabled ? "software" : info.backend;

  return info;
}

/// 采样进程树资源：宿主与各类型 CEF 子进程的 CPU% 与整树工作集。
/// 首次调用仅建立各进程 CPU 基线并返回全零指标（不产出百分比），
/// 此后返回自上次采样以来的增量占用率（多核下可超过 100%）。
/// @return 当前进程树指标。
ProcessTreeMetrics SampleProcessTreeMetrics() {
  ProcessTreeMetrics metrics;
  const auto now = std::chrono::steady_clock::now();
  auto& baselines = ProcessCpuBaselines();

  // 宿主进程。
  double host_cpu = 0.0;
  SamplePidCpuPercent(0, now, baselines, &host_cpu);
  metrics.host_cpu = host_cpu;
  const uint64_t host_working_set = SamplePidWorkingSet(0);
  metrics.host_working_set_mb =
      static_cast<double>(host_working_set) / 1048576.0;
  uint64_t tree_working_set = host_working_set;

  // CEF 子进程：按命令行类型拆分 CPU。
  const std::wstring current_base = CurrentProcessBaseNameLower();
  const std::wstring cef_subprocess_base = L"offscreen_cef_subprocess.exe";
  HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = ::Process32FirstW(snapshot, &entry); ok;
         ok = ::Process32NextW(snapshot, &entry)) {
      std::wstring name(entry.szExeFile);
      for (wchar_t& c : name) {
        c = static_cast<wchar_t>(::towlower(c));
      }
      if (entry.th32ProcessID == ::GetCurrentProcessId()) {
        continue;
      }
      if (name != current_base && name != cef_subprocess_base) {
        continue;
      }
      double cpu = 0.0;
      SamplePidCpuPercent(entry.th32ProcessID, now, baselines, &cpu);
      const uint64_t ws = SamplePidWorkingSet(entry.th32ProcessID);
      tree_working_set += ws;

      const std::wstring command_line =
          ReadProcessCommandLine(entry.th32ProcessID);
      if (command_line.find(L"--type=renderer") != std::wstring::npos) {
        metrics.renderer_cpu_total += cpu;
      } else if (command_line.find(L"--type=gpu-process") !=
                 std::wstring::npos) {
        metrics.gpu_process_cpu += cpu;
      }
      metrics.process_tree_cpu += cpu;
    }
    ::CloseHandle(snapshot);
  }

  metrics.process_tree_cpu += metrics.host_cpu;
  metrics.process_tree_working_set_mb =
      static_cast<double>(tree_working_set) / 1048576.0;
  return metrics;
}

/// 采样进程 CPU 占用率。首次调用仅建立基线并返回 false（不产出百分比），
/// 否则按进程寿命累计算出的首行会是假的大值；此后返回自上次采样以来的
/// 增量占用率（多核下可超过 100%）。
/// @param cpu_percent 输出 CPU 百分比。
/// @return 本次是否产出了有效百分比。
bool SampleCpuPercent(double* cpu_percent) {
  const auto now = std::chrono::steady_clock::now();
  return SamplePidCpuPercent(0, now, ProcessCpuBaselines(), cpu_percent);
}

void SetRenderStatsLogFile(const std::wstring& path) {
  std::lock_guard<std::mutex> lock(RenderStatsLogMutex());
  HANDLE& current_log_file = RenderStatsLogFileHandle();
  if (current_log_file != INVALID_HANDLE_VALUE) {
    ::CloseHandle(current_log_file);
    current_log_file = INVALID_HANDLE_VALUE;
  }
  current_log_file = OpenRenderStatsLogFileLocked(path);
}

void SetRenderStatsLogFileToApplicationDirectory() {
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
      log_dir + L"offscreen_render_stats_" + StringToWide(FormatHourKey()) +
      L".csv";

  std::lock_guard<std::mutex> lock(RenderStatsLogMutex());
  HANDLE& current_log_file = RenderStatsLogFileHandle();
  if (current_log_file != INVALID_HANDLE_VALUE) {
    ::CloseHandle(current_log_file);
    current_log_file = INVALID_HANDLE_VALUE;
  }
  current_log_file = OpenRenderStatsLogFileLocked(log_path);
  RenderStatsLogCurrentHour() = FormatHourKey();
  RenderStatsLogDirectory() = WideToUtf8(log_dir);
}

void RotateRenderStatsLogIfHourChanged() {
  std::lock_guard<std::mutex> lock(RenderStatsLogMutex());
  const std::string hour = FormatHourKey();
  if (hour == RenderStatsLogCurrentHour() ||
      RenderStatsLogDirectory().empty()) {
    return;
  }
  HANDLE& current_log_file = RenderStatsLogFileHandle();
  if (current_log_file != INVALID_HANDLE_VALUE) {
    ::CloseHandle(current_log_file);
    current_log_file = INVALID_HANDLE_VALUE;
  }
  const std::wstring log_path =
      StringToWide(RenderStatsLogDirectory() + "offscreen_render_stats_" +
                   hour + ".csv");
  current_log_file = OpenRenderStatsLogFileLocked(log_path);
  RenderStatsLogCurrentHour() = hour;
}

void RenderStatsLogWrite(const RenderStatsSnapshot& snapshot) {
  std::lock_guard<std::mutex> lock(RenderStatsLogMutex());
  const HANDLE log_file = RenderStatsLogFileHandle();
  if (log_file == INVALID_HANDLE_VALUE) {
    return;
  }

  const ProcessTreeMetrics process_metrics = SampleProcessTreeMetrics();
  const GpuBackendInfo gpu_info = SampleGpuBackendInfo();

  const std::string line = FormatRenderStatsCsvRow(
      snapshot, process_metrics, gpu_info);
  DWORD bytes_written = 0;
  ::WriteFile(log_file, line.data(), static_cast<DWORD>(line.size()),
              &bytes_written, nullptr);
}

std::string FormatRenderStatsCsvHeader() {
  return "timestamp,run_id,elapsed_ms,cef_paint_fps,qt_paint_fps,"
         "window_frames,window_paint_events,accelerated_frames,"
         "dropped_frames,coalesced_frames,extra_qt_paints,drop_reason,"
         "onpaint_avg_ms,set_view_image_avg_ms,schedule_delay_avg_ms,"
         "snapshot_avg_ms,draw_image_avg_ms,"
         "frame_interval_avg_ms,dirty_area_avg_px,dirty_count_avg,"
         "buffer_width,buffer_height,buffer_bytes,"
         "host_cpu,host_working_set_mb,renderer_cpu_total,gpu_process_cpu,"
         "process_tree_cpu,process_tree_working_set_mb,"
         "gpu_requested,gpu_backend,gpu_actual_backend,"
         "gpu_init_error_count,gpu_process_restarted_count,"
         "cef_subprocess_count,gpu_present_path,d3d_to_gl_frames,"
         "cpu_readback_fallback_frames,total_d3d_to_gl_frames,"
         "total_cpu_readback_fallback_frames";
}

/// 选择窗口内掉帧的主导原因。
/// @param snapshot 渲染统计快照。
/// @return 掉帧原因枚举名；无掉帧时返回 "none"。
std::string PickDropReason(const RenderStatsSnapshot& snapshot) {
  uint64_t best = snapshot.drop_reason_onpaint_slow;
  std::string reason = best > 0 ? "onpaint_slow" : "";
  if (snapshot.drop_reason_backlog > best) {
    best = snapshot.drop_reason_backlog;
    reason = "backlog";
  }
  if (snapshot.drop_reason_interval > best) {
    best = snapshot.drop_reason_interval;
    reason = "interval_gap";
  }
  return reason.empty() ? "none" : reason;
}

std::string FormatRenderStatsCsvRow(const RenderStatsSnapshot& snapshot,
                                    double cpu_percent,
                                    uint64_t working_set_bytes) {
  ProcessTreeMetrics process_metrics;
  process_metrics.host_cpu = cpu_percent;
  process_metrics.host_working_set_mb =
      static_cast<double>(working_set_bytes) / 1048576.0;
  process_metrics.process_tree_cpu = cpu_percent;
  process_metrics.process_tree_working_set_mb =
      static_cast<double>(working_set_bytes) / 1048576.0;
  return FormatRenderStatsCsvRow(snapshot, process_metrics,
                                 SampleGpuBackendInfo());
}

std::string FormatRenderStatsCsvRow(const RenderStatsSnapshot& snapshot,
                                    double cpu_percent,
                                    uint64_t working_set_bytes,
                                    const GpuBackendInfo& gpu_info) {
  ProcessTreeMetrics process_metrics;
  process_metrics.host_cpu = cpu_percent;
  process_metrics.host_working_set_mb =
      static_cast<double>(working_set_bytes) / 1048576.0;
  process_metrics.process_tree_cpu = cpu_percent;
  process_metrics.process_tree_working_set_mb =
      static_cast<double>(working_set_bytes) / 1048576.0;
  return FormatRenderStatsCsvRow(snapshot, process_metrics, gpu_info);
}

std::string FormatRenderStatsCsvRow(const RenderStatsSnapshot& snapshot,
                                    const ProcessTreeMetrics& process_metrics,
                                    const GpuBackendInfo& gpu_info) {
  constexpr std::size_t kStageCount =
      static_cast<std::size_t>(RenderStage::kCount);
  const double dirty_frames = static_cast<double>(snapshot.dirty_frames);

  std::ostringstream stream;
  stream << CsvField(FormatTimestampHMSMM()) << ','
         << CsvField(RunLifecycleId()) << ','
         << static_cast<int64_t>(snapshot.window_seconds * 1000.0) << ','
         << snapshot.fps << ',' << snapshot.qt_fps << ','
         << snapshot.window_frames << ',' << snapshot.window_paint_events << ','
         << snapshot.window_accelerated_frames << ','
         << snapshot.window_dropped_frames << ','
         << snapshot.window_coalesced_frames << ','
         << snapshot.window_extra_qt_paints << ','
         << CsvField(PickDropReason(snapshot)) << ',';
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
         << ',' << process_metrics.host_cpu << ','
         << process_metrics.host_working_set_mb << ','
         << process_metrics.renderer_cpu_total << ','
         << process_metrics.gpu_process_cpu << ','
         << process_metrics.process_tree_cpu << ','
         << process_metrics.process_tree_working_set_mb << ','
         << (gpu_info.gpu_requested ? 1 : 0) << ','
         << CsvField(gpu_info.backend) << ','
         << CsvField(gpu_info.actual_backend) << ','
         << gpu_info.gpu_init_error_count << ','
         << gpu_info.gpu_process_restarted_count << ','
         << gpu_info.subprocess_count << ','
         << CsvField(snapshot.gpu_present_path) << ','
         << snapshot.window_d3d_to_gl_frames << ','
         << snapshot.window_cpu_readback_fallback_frames << ','
         << snapshot.total_d3d_to_gl_frames << ','
         << snapshot.total_cpu_readback_fallback_frames << '\n';
  return stream.str();
}

}  // namespace offscreen
