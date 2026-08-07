#include "browser/render_stats_log.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include <windows.h>

namespace {

template <typename A, typename B>
void expect_eq(A actual, B expected, const char* label) {
  if (actual != expected) {
    std::cerr << label << " expected [" << expected << "] but got [" << actual
              << "]\n";
    std::exit(1);
  }
}

void expect_true(bool actual, const char* label) {
  if (!actual) {
    std::cerr << label << " expected true but got false\n";
    std::exit(1);
  }
}

// 构造一个字段已知的快照，便于断言行格式。
offscreen::RenderStatsSnapshot MakeSnapshot() {
  offscreen::RenderStatsSnapshot s;
  s.enabled = true;
  s.window_frames = 30;
  s.window_paint_events = 28;
  s.window_accelerated_frames = 5;
  s.gpu_present_path = "wgl_dx_interop";
  s.window_d3d_to_gl_frames = 4;
  s.window_cpu_readback_fallback_frames = 1;
  s.total_d3d_to_gl_frames = 40;
  s.total_cpu_readback_fallback_frames = 3;
  s.window_dropped_frames = 2;
  s.window_seconds = 1.0;
  s.fps = 30.0;
  s.qt_fps = 28.0;
  s.frame_interval_ms.count = 10;
  s.frame_interval_ms.sum_ms = 333.0;  // avg = 33.3
  // 阶段顺序：onpaint, set_view_image, schedule_delay, snapshot, draw_image。
  s.stages[0].count = 30;
  s.stages[0].sum_ms = 60.0;  // onpaint avg = 2.0
  s.stages[1].count = 30;
  s.stages[1].sum_ms = 90.0;  // set_view_image avg = 3.0
  s.stages[2].count = 28;
  s.stages[2].sum_ms = 14.0;  // schedule_delay avg = 0.5
  s.stages[3].count = 28;
  s.stages[3].sum_ms = 56.0;  // snapshot avg = 2.0
  s.stages[4].count = 28;
  s.stages[4].sum_ms = 2.8;  // draw_image avg = 0.1
  s.last_width = 1920;
  s.last_height = 1080;
  s.dirty_frames = 10;
  s.dirty_area_sum_px = 1000;  // dirty_area_avg = 100
  s.dirty_rect_count_sum = 20;  // dirty_count_avg = 2
  return s;
}

// --- 表头 ---

void test_format_header() {
  const std::string header = offscreen::FormatRenderStatsCsvHeader();
  expect_eq(header,
            "timestamp,run_id,elapsed_ms,cef_paint_fps,qt_paint_fps,"
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
            "total_cpu_readback_fallback_frames",
            "csv header");
}

// --- 单行格式化：数值列与阶段均值 ---

void test_format_row() {
  const offscreen::RenderStatsSnapshot s = MakeSnapshot();
  const std::string row =
      offscreen::FormatRenderStatsCsvRow(s, 25.5, 104857600);
  // 时间戳前缀为 HH:MM:SS.mmm（第 3 个字符是冒号）。
  expect_true(row.size() > 12, "row has timestamp prefix");
  expect_true(row[2] == ':', "timestamp format HH:MM:SS.mmm");
  // elapsed_ms = window_seconds*1000 = 1000；cef_fps=30、qt_fps=28、frames/pe。
  expect_true(row.find("1000,30,28,30,28,") != std::string::npos,
              "row elapsed/fps/frames");
  // 加速帧=5、掉帧=2、合帧=0、额外 Qt 重绘=0、掉帧原因=none。
  expect_true(row.find("30,28,5,2,0,0,none,") != std::string::npos,
              "row accelerated/dropped/coalesced/drop reason");
  // 阶段均值：2,3,0.5,2,0.1。
  expect_true(row.find("2,3,0.5,2,0.1,") != std::string::npos,
              "row stage averages");
  // frame_interval=33.3、dirty_area_avg=100、dirty_count_avg=2。
  expect_true(row.find("33.3,100,2,") != std::string::npos,
              "row interval + dirty averages");
  // buffer 尺寸与字节：1920x1080 且 1920*1080*4 = 8294400。
  expect_true(row.find("1920,1080,8294400,") != std::string::npos,
              "row buffer size and bytes");
  // cpu=25.5、工作集 104857600 B 即 100 MB。
  expect_true(row.find("25.5,100,") != std::string::npos,
              "row cpu and working set");
  // GPU 列：默认 gpu_requested=1、backend=default、子进程数 >= 0。
  expect_true(row.find(",1,default,") != std::string::npos,
              "row gpu columns");
  expect_true(row.find(",wgl_dx_interop,4,1,40,3\n") != std::string::npos,
              "row GPU presentation columns");
}

// --- 显式 GPU 信息重载 ---

void test_format_row_with_explicit_gpu_info() {
  const offscreen::RenderStatsSnapshot s = MakeSnapshot();
  offscreen::GpuBackendInfo gpu;
  gpu.gpu_requested = false;
  gpu.backend = "angle/d3d11";
  gpu.actual_backend = "software";
  gpu.gpu_init_error_count = 2;
  gpu.gpu_process_restarted_count = 1;
  gpu.subprocess_count = 7;
  const std::string row =
      offscreen::FormatRenderStatsCsvRow(s, 0.0, 0, gpu);
  expect_true(row.find(",0,angle/d3d11,software,2,1,7,wgl_dx_interop,"
                       "4,1,40,3\n") != std::string::npos,
              "row explicit gpu info");
}

// --- dirty 除零保护：dirty_frames=0 时均值为 0 ---

void test_format_row_dirty_guard() {
  offscreen::RenderStatsSnapshot s;
  s.last_width = 100;
  s.last_height = 50;
  s.dirty_frames = 0;
  s.dirty_area_sum_px = 123456;
  s.dirty_rect_count_sum = 99;

  const std::string row = offscreen::FormatRenderStatsCsvRow(s, 0.0, 0);
  expect_true(row.find("123456") == std::string::npos,
              "dirty area avg guarded to zero");
  // dirty_area_avg=0、dirty_count_avg=0，且 buffer_bytes = 100*50*4 = 20000。
  expect_true(row.find(",0,0,100,50,20000,") != std::string::npos,
              "dirty averages zero and buffer present");
}

// --- 落盘：表头 + 行数 + 内容，结束后清理临时文件 ---

void test_write_to_disk() {
  wchar_t temp_dir[MAX_PATH] = {};
  ::GetTempPathW(MAX_PATH, temp_dir);
  wchar_t temp_file[MAX_PATH] = {};
  ::GetTempFileNameW(temp_dir, L"rst", 0, temp_file);
  const std::wstring path(temp_file);

  offscreen::SetRenderStatsLogFile(path);

  const offscreen::RenderStatsSnapshot s = MakeSnapshot();
  offscreen::RenderStatsLogWrite(s);
  offscreen::RenderStatsLogWrite(s);

  std::ifstream in(path, std::ios::binary);
  expect_true(in.is_open(), "log file opened for read");

  std::string header_line;
  std::getline(in, header_line);
  if (header_line.size() >= 3 &&
      static_cast<unsigned char>(header_line[0]) == 0xEF &&
      static_cast<unsigned char>(header_line[1]) == 0xBB &&
      static_cast<unsigned char>(header_line[2]) == 0xBF) {
    header_line = header_line.substr(3);
  }
  expect_eq(header_line, offscreen::FormatRenderStatsCsvHeader(),
            "file header line");

  std::string row1;
  std::string row2;
  std::getline(in, row1);
  std::getline(in, row2);
  expect_true(row1.find("1920,1080,8294400") != std::string::npos,
              "row1 contains buffer");
  expect_true(row2.find("1920,1080,8294400") != std::string::npos,
              "row2 contains buffer");
  expect_true(row1.find("1000,30,28,30,28,") != std::string::npos,
              "row1 contains metrics");

  std::string extra;
  expect_true(!std::getline(in, extra), "no extra lines after two rows");

  in.close();
  ::DeleteFileW(path.c_str());
}

// --- 回归：修复前 SampleCpuPercent 先更新 baseline.wall 再算间隔，导致
// wall_seconds 恒为 0、函数永远返回 false、cpu_percent 恒为 0。该测试通过
// ~100ms 忙等保证两次调用间有真实 CPU 消耗，断言第二次采样产生非零百分比。 ---

void test_cpu_sample_regression() {
  double cpu = 0.0;
  (void)offscreen::SampleCpuPercent(&cpu);  // 建立/刷新基线，返回值可忽略
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
  while (std::chrono::steady_clock::now() < deadline) {
    // 忙等约 100ms，保证下一采样间隔内进程消耗了真实 CPU。
  }
  const bool sampled = offscreen::SampleCpuPercent(&cpu);
  expect_true(sampled, "cpu percent sampled after busy wait");
  expect_true(cpu > 1.0, "cpu percent non-zero after busy wait");
}

// --- GPU 环境信息采样：命令行开关 + 子进程计数 ---

void test_gpu_backend_info() {
  const offscreen::GpuBackendInfo info = offscreen::SampleGpuBackendInfo();
  // 测试进程未传 --disable-gpu 且未指定 use-angle/use-gl。
  expect_true(info.gpu_requested, "gpu requested by default");
  expect_eq(info.backend, std::string("default"), "default backend");
  // 子进程计数 >= 0（本进程不算；测试进程名下不应有额外子进程）。
  expect_true(info.subprocess_count >= 0, "subprocess count non-negative");
  // 新字段：实际后端非空、错误计数与重启计数非负。
  expect_true(!info.actual_backend.empty(), "actual backend non-empty");
  expect_true(info.gpu_init_error_count >= 0, "gpu init error count non-negative");
  expect_true(info.gpu_process_restarted_count >= 0,
              "gpu process restart count non-negative");
}

// --- 进程树指标采样：首次建立基线，之后可产出非负值 ---

void test_process_tree_metrics() {
  const offscreen::ProcessTreeMetrics first =
      offscreen::SampleProcessTreeMetrics();
  expect_true(first.host_cpu >= 0.0, "host cpu non-negative");
  expect_true(first.process_tree_working_set_mb > 0.0,
              "tree working set positive");
  // 二次采样（同一秒内）不应为负。
  const offscreen::ProcessTreeMetrics second =
      offscreen::SampleProcessTreeMetrics();
  expect_true(second.host_cpu >= 0.0, "second host cpu non-negative");
  expect_true(second.process_tree_cpu >= 0.0, "tree cpu non-negative");
}

}  // namespace

int main() {
  test_format_header();
  test_format_row();
  test_format_row_with_explicit_gpu_info();
  test_format_row_dirty_guard();
  test_write_to_disk();
  test_cpu_sample_regression();
  test_gpu_backend_info();
  test_process_tree_metrics();
  return 0;
}
