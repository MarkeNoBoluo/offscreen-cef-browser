#include "browser/render_stats.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

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

void expect_dbl_near(double actual, double expected, double tolerance,
                     const char* label) {
  if (std::abs(actual - expected) > tolerance) {
    std::cerr << label << " expected ~[" << expected << "] but got [" << actual
              << "]\n";
    std::exit(1);
  }
}

using offscreen::RenderStage;

int StageIndex(RenderStage stage) { return static_cast<int>(stage); }

void PushFrame(offscreen::RenderStats& stats, int width, int height,
               int dirty_count, int64_t dirty_area, bool is_popup) {
  stats.OnPaintBegin(width, height, dirty_count, dirty_area, is_popup);
  stats.OnSetViewImageBegin();
  stats.OnSetViewImageDone();
  stats.OnPaintEnd();
}

// --- 默认开启与开关切换 ---

void test_default_enabled_and_toggle() {
  offscreen::RenderStats stats;
  expect_true(stats.enabled(), "enabled by default");

  stats.Disable();
  expect_true(!stats.enabled(), "disabled after Disable");

  stats.Enable();
  expect_true(stats.enabled(), "enabled after Enable");
}

// --- 单帧记录 ---

void test_single_frame_recorded() {
  offscreen::RenderStats stats;
  stats.OnPaintBegin(1920, 1080, 3, 500000, false);
  stats.OnSetViewImageBegin();
  stats.OnSetViewImageDone();
  stats.OnPaintEnd();

  const offscreen::RenderStatsSnapshot snap = stats.Snapshot();
  expect_eq(snap.total_frames, static_cast<uint64_t>(1), "total frames");
  expect_eq(snap.window_frames, static_cast<uint64_t>(1), "window frames");
  expect_eq(snap.recent_frames.size(), static_cast<std::size_t>(1), "ring size");

  const auto& sample = snap.recent_frames[0];
  expect_eq(sample.sequence, static_cast<uint64_t>(1), "sample sequence");
  expect_eq(sample.width, 1920, "sample width");
  expect_eq(sample.height, 1080, "sample height");
  expect_eq(sample.dirty_count, 3, "sample dirty count");
  expect_eq(sample.dirty_area_px, static_cast<int64_t>(500000), "dirty area");
  expect_eq(sample.buffer_bytes, static_cast<int64_t>(1920 * 1080 * 4),
            "buffer bytes");
  expect_true(!sample.is_popup, "not popup");
  expect_true(sample.stage_ms[StageIndex(RenderStage::kOnPaint)] >= 0.0,
              "onpaint stage observed");
  expect_true(sample.stage_ms[StageIndex(RenderStage::kSetViewImage)] >= 0.0,
              "setview stage observed");
  expect_eq(sample.stage_ms[StageIndex(RenderStage::kScheduleDelay)], -1.0,
            "schedule not observed yet");

  expect_eq(snap.last_width, 1920, "last width");
  expect_eq(snap.last_height, 1080, "last height");
  expect_eq(snap.last_dirty_count, 3, "last dirty count");
  expect_eq(snap.last_dirty_area_px, static_cast<int64_t>(500000),
            "last dirty area");
}

// --- 帧间隔与 FPS ---

void test_frame_interval_and_fps() {
  offscreen::RenderStats stats;
  stats.OnPaintBegin(100, 50, 1, 100, false);
  stats.OnSetViewImageBegin();
  stats.OnSetViewImageDone();
  stats.OnPaintEnd();

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  PushFrame(stats, 100, 50, 1, 100, false);

  const offscreen::RenderStatsSnapshot snap = stats.Snapshot();
  expect_eq(snap.window_frames, static_cast<uint64_t>(2), "two window frames");
  // 首帧不聚合帧间隔；第二帧间隔约 10ms。
  expect_eq(snap.frame_interval_ms.count, static_cast<uint64_t>(1),
            "interval count skips first frame");
  expect_true(snap.frame_interval_ms.avg_ms() >= 5.0,
              "interval avg >= 5ms after 10ms sleep");
  expect_true(snap.fps > 0.0, "fps positive");
  const double expected_fps =
      static_cast<double>(snap.window_frames) / snap.window_seconds;
  expect_dbl_near(snap.fps, expected_fps, 0.001, "fps matches frames/seconds");
}

// --- 阶段 min/max/avg/last 聚合 ---

void test_stage_aggregation() {
  offscreen::RenderStats stats;
  for (int i = 0; i < 5; ++i) {
    stats.OnPaintBegin(640, 480, 2, 1000, false);
    stats.OnSetViewImageBegin();
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    stats.OnSetViewImageDone();
    stats.OnPaintEnd();
  }

  const offscreen::RenderStatsSnapshot snap = stats.Snapshot();
  expect_eq(snap.window_frames, static_cast<uint64_t>(5), "five frames");
  expect_eq(snap.stages[StageIndex(RenderStage::kOnPaint)].count,
            static_cast<uint64_t>(5), "onpaint count");
  expect_eq(snap.stages[StageIndex(RenderStage::kSetViewImage)].count,
            static_cast<uint64_t>(5), "setview count");
  const auto& onpaint = snap.stages[StageIndex(RenderStage::kOnPaint)];
  expect_true(onpaint.max_ms >= onpaint.min_ms, "onpaint max >= min");
  expect_true(onpaint.avg_ms() >= onpaint.min_ms, "onpaint avg >= min");
  expect_true(onpaint.avg_ms() <= onpaint.max_ms, "onpaint avg <= max");
  const auto& setview = snap.stages[StageIndex(RenderStage::kSetViewImage)];
  expect_true(setview.last_ms > 0.0, "setview last > 0 after microsleep");
}

// --- 环形缓冲淘汰 ---

void test_ring_eviction() {
  offscreen::RenderStats stats(3);
  for (int i = 1; i <= 5; ++i) {
    PushFrame(stats, 10 + i, 20, 1, 10, false);
  }

  const offscreen::RenderStatsSnapshot snap = stats.Snapshot();
  expect_eq(snap.recent_frames.size(), static_cast<std::size_t>(3), "ring cap 3");
  expect_eq(snap.recent_frames[0].sequence, static_cast<uint64_t>(3),
            "oldest retained sequence");
  expect_eq(snap.recent_frames[2].sequence, static_cast<uint64_t>(5),
            "newest retained sequence");
  expect_eq(snap.total_frames, static_cast<uint64_t>(5), "total preserves 5");
}

// --- draw 阶段写回 ---

void test_draw_stages_written_back() {
  offscreen::RenderStats stats;
  PushFrame(stats, 100, 100, 1, 100, false);

  stats.OnPaintEventBegin();
  stats.OnSnapshotBegin();
  stats.OnSnapshotDone();
  stats.OnDrawImageBegin();
  stats.OnDrawImageDone();
  stats.OnPaintEventEnd();

  const offscreen::RenderStatsSnapshot snap = stats.Snapshot();
  expect_eq(snap.recent_frames.size(), static_cast<std::size_t>(1), "one sample");
  const auto& sample = snap.recent_frames[0];
  expect_true(sample.stage_ms[StageIndex(RenderStage::kScheduleDelay)] >= 0.0,
              "schedule delay written back");
  expect_true(sample.stage_ms[StageIndex(RenderStage::kSnapshot)] >= 0.0,
              "snapshot written back");
  expect_true(sample.stage_ms[StageIndex(RenderStage::kDrawImage)] >= 0.0,
              "draw written back");
  expect_eq(snap.stages[StageIndex(RenderStage::kScheduleDelay)].count,
            static_cast<uint64_t>(1), "schedule aggregated");
  expect_eq(snap.stages[StageIndex(RenderStage::kSnapshot)].count,
            static_cast<uint64_t>(1), "snapshot aggregated");
  expect_eq(snap.stages[StageIndex(RenderStage::kDrawImage)].count,
            static_cast<uint64_t>(1), "draw aggregated");
}

// --- 窗口重置 ---

void test_window_reset_preserves_total() {
  offscreen::RenderStats stats;
  for (int i = 0; i < 3; ++i) {
    PushFrame(stats, 200, 100, 1, 50, false);
  }
  const offscreen::RenderStatsSnapshot first = stats.SnapshotAndResetWindow();
  expect_eq(first.window_frames, static_cast<uint64_t>(3), "window before reset");
  expect_eq(first.total_frames, static_cast<uint64_t>(3), "total before reset");

  PushFrame(stats, 200, 100, 1, 50, false);
  const offscreen::RenderStatsSnapshot second = stats.Snapshot();
  expect_eq(second.window_frames, static_cast<uint64_t>(1), "window after reset");
  expect_eq(second.total_frames, static_cast<uint64_t>(4), "total preserved");
}

// --- 禁用后不记录 ---

void test_disabled_records_nothing() {
  offscreen::RenderStats stats;
  stats.Disable();
  for (int i = 0; i < 5; ++i) {
    PushFrame(stats, 100, 100, 1, 10, false);
  }
  offscreen::RenderStatsSnapshot snap = stats.Snapshot();
  expect_eq(snap.total_frames, static_cast<uint64_t>(0), "disabled records 0");
  expect_eq(snap.recent_frames.size(), static_cast<std::size_t>(0), "ring empty");

  stats.Enable();
  PushFrame(stats, 100, 100, 1, 10, false);
  snap = stats.Snapshot();
  expect_eq(snap.total_frames, static_cast<uint64_t>(1), "records after enable");
}

// --- 双线程并发 2000 帧 ---

void test_concurrent_threads() {
  offscreen::RenderStats stats;
  auto worker = [&stats](int frames) {
    for (int i = 0; i < frames; ++i) {
      stats.OnPaintBegin(640, 480, 1, 1000, false);
      stats.OnSetViewImageBegin();
      stats.OnSetViewImageDone();
      stats.OnPaintEnd();
    }
  };
  std::thread thread_a(worker, 1000);
  std::thread thread_b(worker, 1000);
  thread_a.join();
  thread_b.join();

  expect_eq(stats.total_frames(), static_cast<uint64_t>(2000),
            "concurrent total frames");
  const offscreen::RenderStatsSnapshot snap = stats.Snapshot();
  expect_eq(snap.window_frames, static_cast<uint64_t>(2000),
            "concurrent window frames");
  expect_eq(snap.recent_frames.size(),
            offscreen::RenderStats::kDefaultRingCapacity, "ring full at capacity");
}

// --- FormatRenderStatsSummary ---

void test_format_summary_contains_fields() {
  offscreen::RenderStats stats;
  PushFrame(stats, 1920, 1080, 3, 500000, false);
  stats.OnPaintEventBegin();
  stats.OnSnapshotBegin();
  stats.OnSnapshotDone();
  stats.OnDrawImageBegin();
  stats.OnDrawImageDone();
  stats.OnPaintEventEnd();
  PushFrame(stats, 1920, 1080, 2, 300000, false);

  const std::string text = offscreen::FormatRenderStatsSummary(stats.Snapshot());
  expect_true(text.find("RenderStats") != std::string::npos, "has prefix");
  expect_true(text.find("buf=") != std::string::npos, "has buf");
  expect_true(text.find("frames=") != std::string::npos, "has frames");
  expect_true(text.find("pe=") != std::string::npos, "has pe");
  expect_true(text.find("fps=") != std::string::npos, "has fps");
  expect_true(text.find("onpaint") != std::string::npos, "has onpaint");
  expect_true(text.find("set_view_image") != std::string::npos,
              "has set_view_image");
  expect_true(text.find("schedule_delay") != std::string::npos,
              "has schedule_delay");
  expect_true(text.find("snapshot") != std::string::npos, "has snapshot");
  expect_true(text.find("draw_image") != std::string::npos, "has draw_image");
  expect_true(text.find("dirty_avg") != std::string::npos, "has dirty_avg");
  expect_true(text.find("count_avg") != std::string::npos, "has count_avg");
}

}  // namespace

int main() {
  test_default_enabled_and_toggle();
  test_single_frame_recorded();
  test_frame_interval_and_fps();
  test_stage_aggregation();
  test_ring_eviction();
  test_draw_stages_written_back();
  test_window_reset_preserves_total();
  test_disabled_records_nothing();
  test_concurrent_threads();
  test_format_summary_contains_fields();
  return 0;
}
