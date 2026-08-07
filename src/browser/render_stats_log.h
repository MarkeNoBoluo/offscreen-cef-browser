#pragma once

#include <cstdint>
#include <string>

#include "browser/render_stats.h"

namespace offscreen {

/// 将 RenderStats 周期快照导出为宽表 CSV。每行对应一个统计窗口
/// （与 RenderStats::SnapshotAndResetWindow 周期一致），包含各阶段平均耗时、
/// 帧率、脏区面积与进程 CPU/工作集，用于建立渲染性能 before/after 基线。
/// timestamp 由写入器在落盘时生成，不进结构体。

/// 设置 RenderStats 统计 CSV 日志文件路径。底层 OPEN_ALWAYS 打开；文件为空时
/// 先写 UTF-8 BOM（Excel 兼容）+ 表头行。
/// @param path 目标文件完整路径。
void SetRenderStatsLogFile(const std::wstring& path);

/// 在可执行文件目录创建或追加按小时切分的 offscreen_render_stats_YYYYMMDD_HH.csv。
void SetRenderStatsLogFileToApplicationDirectory();

/// 检查是否跨小时：跨小时时自动关闭旧文件并创建新小时文件（含 BOM + 表头）。
/// 由秒级定时器调用，避免热路径写入时做时间判断。
void RotateRenderStatsLogIfHourChanged();

/// 追加一行 RenderStats 周期快照（互斥锁 + 常开句柄，追加一行 CSV）。
/// 内部采样进程树 CPU/工作集与 GPU 实际状态后交给纯格式化函数。
/// @param snapshot 待写入的周期快照。
void RenderStatsLogWrite(const RenderStatsSnapshot& snapshot);

/// 采样进程 CPU 占用率。首次调用仅建立基线并返回 false（不产出百分比），
/// 此后返回自上次采样以来的增量占用率（多核下可超过 100%）。
/// @param cpu_percent 输出 CPU 百分比。
/// @return 本次是否产出了有效百分比。
bool SampleCpuPercent(double* cpu_percent);

// GPU 渲染路径环境信息（每次写入 CSV 时实时采样，供 GPU 路径验收使用）。
struct GpuBackendInfo {
  bool gpu_requested = true;         // 命令行未禁用 GPU（无 --disable-gpu）
  std::string backend = "default";   // 命令行 --use-angle/--use-gl 参数值，未指定为 default
  int subprocess_count = 0;          // 存活的 CEF 子进程数（含 gpu/renderer）
  std::string actual_backend = "default";  // 实际渲染后端：从 cef.log 推断
                                           // （software=GL 禁用回退；否则沿用命令行 backend）
  int gpu_init_error_count = 0;      // cef.log 中 GPU 初始化/GL 错误行累计数
  int gpu_process_restarted_count = 0;  // GPU 进程重启累计次数（pid 消失即计一次）
};

// 进程树资源指标（宿主 + CEF 子进程按类型拆分，多核下 CPU 可超过 100%）。
struct ProcessTreeMetrics {
  double host_cpu = 0.0;                  // 宿主进程 CPU%
  double host_working_set_mb = 0.0;       // 宿主进程工作集（MB）
  double renderer_cpu_total = 0.0;        // 所有 renderer 子进程 CPU% 之和
  double gpu_process_cpu = 0.0;           // GPU 子进程 CPU%
  double process_tree_cpu = 0.0;          // 整棵进程树 CPU% 之和
  double process_tree_working_set_mb = 0.0;  // 整棵进程树工作集（MB）
};

/// 采样 GPU 环境信息：命令行开关 + 存活 CEF 子进程计数。
/// 命令行开关部分结果按进程生命周期缓存（启动后不再变化）。
/// @return 当前 GPU 环境信息。
GpuBackendInfo SampleGpuBackendInfo();

/// 采样进程树资源：宿主与各类型 CEF 子进程的 CPU% 与整树工作集。
/// 首次调用仅建立各进程 CPU 基线并返回全零指标（不产出百分比），
/// 此后返回自上次采样以来的增量占用率。
/// @return 当前进程树指标。
ProcessTreeMetrics SampleProcessTreeMetrics();

/// 返回 CSV 表头行文本（34 列，不含末尾换行）。
/// @return 表头行文本。
std::string FormatRenderStatsCsvHeader();

/// 将快照格式化为单行 CSV（含末尾换行）。CPU% 为进程级（多核下可超过 100）；
/// 工作集单位为 MB。
/// @param snapshot 待格式化的快照。
/// @param cpu_percent 宿主进程 CPU 占用率（%，可为 0 当本次无有效采样）。
/// @param working_set_bytes 宿主进程工作集（字节）。
/// @return 单行 UTF-8 CSV 文本。
std::string FormatRenderStatsCsvRow(const RenderStatsSnapshot& snapshot,
                                    double cpu_percent,
                                    uint64_t working_set_bytes);

/// 将快照格式化为单行 CSV（含末尾换行），并显式指定 GPU 环境信息。
/// @param snapshot 待格式化的快照。
/// @param cpu_percent 宿主进程 CPU 占用率（%，可为 0 当本次无有效采样）。
/// @param working_set_bytes 宿主进程工作集（字节）。
/// @param gpu_info 本次采样到的 GPU 环境信息。
/// @return 单行 UTF-8 CSV 文本。
std::string FormatRenderStatsCsvRow(const RenderStatsSnapshot& snapshot,
                                    double cpu_percent,
                                    uint64_t working_set_bytes,
                                    const GpuBackendInfo& gpu_info);

/// 将快照格式化为单行 CSV（含末尾换行），使用完整的进程树指标与 GPU 信息。
/// @param snapshot 待格式化的快照。
/// @param process_metrics 本次采样到的进程树资源指标。
/// @param gpu_info 本次采样到的 GPU 环境信息。
/// @return 单行 UTF-8 CSV 文本。
std::string FormatRenderStatsCsvRow(const RenderStatsSnapshot& snapshot,
                                    const ProcessTreeMetrics& process_metrics,
                                    const GpuBackendInfo& gpu_info);

}  // namespace offscreen
