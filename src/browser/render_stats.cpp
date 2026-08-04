#include "browser/render_stats.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <utility>

namespace offscreen {

namespace {

/// steady_clock 当前时刻（整数毫秒）。
int64_t SteadyNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// steady_clock 当前时刻（浮点毫秒，保留亚毫秒精度）。
double SteadyNowMsDouble() {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// 把一次观测累加进阶段聚合。
/// @param stat 目标聚合。
/// @param ms 本次观测耗时（毫秒）。
void RecordStageStat(RenderStageStats& stat, double ms) {
  if (stat.count == 0) {
    stat.min_ms = ms;
    stat.max_ms = ms;
  } else {
    stat.min_ms = std::min(stat.min_ms, ms);
    stat.max_ms = std::max(stat.max_ms, ms);
  }
  stat.sum_ms += ms;
  stat.last_ms = ms;
  ++stat.count;
}

}  // namespace

RenderStats::RenderStats(std::size_t ring_capacity)
    : ring_(ring_capacity > 0 ? ring_capacity : 1) {
  window_begin_ms_ = SteadyNowMsDouble();
}

void RenderStats::Enable() {
  std::lock_guard<std::mutex> lock(mutex_);
  enabled_.store(true, std::memory_order_relaxed);
  ResetWindowLocked(SteadyNowMsDouble());
}

void RenderStats::Disable() {
  std::lock_guard<std::mutex> lock(mutex_);
  enabled_.store(false, std::memory_order_relaxed);
}

bool RenderStats::enabled() const {
  return enabled_.load(std::memory_order_relaxed);
}

void RenderStats::ResetWindowLocked(double now_ms) {
  window_frames_ = 0;
  window_paint_events_ = 0;
  window_begin_ms_ = now_ms;
  last_onpaint_end_ms_ = 0;
  frame_interval_agg_ = {};
  stage_aggs_ = {};
  dirty_area_sum_px_ = 0;
  dirty_rect_count_sum_ = 0;
  dirty_frames_ = 0;
  dispatch_pending_ = false;
}

void RenderStats::OnPaintBegin(int width, int height, int dirty_count,
                               int64_t dirty_area_px, bool is_popup) {
  if (!enabled_.load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> lock(mutex_);
  frame_build_active_ = true;
  onpaint_begin_ms_ = SteadyNowMsDouble();
  setview_active_ = false;
  setview_ms_ = 0;
  frame_width_ = width;
  frame_height_ = height;
  frame_dirty_count_ = dirty_count;
  frame_dirty_area_px_ = dirty_area_px;
  frame_is_popup_ = is_popup;
  last_width_ = width;
  last_height_ = height;
  last_dirty_count_ = dirty_count;
  last_dirty_area_px_ = dirty_area_px;
}

void RenderStats::OnSetViewImageBegin() {
  if (!enabled_.load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> lock(mutex_);
  setview_active_ = true;
  setview_begin_ms_ = SteadyNowMsDouble();
}

void RenderStats::OnSetViewImageDone() {
  if (!enabled_.load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!setview_active_) return;
  setview_active_ = false;
  setview_ms_ = SteadyNowMsDouble() - setview_begin_ms_;
}

void RenderStats::OnPaintEnd() {
  if (!enabled_.load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> lock(mutex_);

  const double now_ms = SteadyNowMsDouble();
  const int64_t now_i = SteadyNowMs();
  // 生产环境 OnPaintEnd 必与 OnPaintBegin 成对；并发测试下交错调用会使
  // frame_build_active_ 短暂为假，此时仍计数并入环，保证计数准确。
  const double onpaint_ms =
      frame_build_active_ ? now_ms - onpaint_begin_ms_ : 0.0;
  frame_build_active_ = false;
  const double interval_ms =
      (last_onpaint_end_ms_ == 0) ? 0.0 : now_ms - last_onpaint_end_ms_;

  RecordStageStat(stage_aggs_[static_cast<int>(RenderStage::kOnPaint)],
                  onpaint_ms);
  if (setview_active_) {
    // OnSetViewImageBegin 被调用但 Done 缺失（防御）：按 0 处理并复位状态。
    setview_active_ = false;
    setview_ms_ = 0;
  }
  RecordStageStat(stage_aggs_[static_cast<int>(RenderStage::kSetViewImage)],
                  setview_ms_);
  if (last_onpaint_end_ms_ != 0) {
    RecordStageStat(frame_interval_agg_, interval_ms);
  }

  ++total_frames_;
  ++window_frames_;
  dirty_area_sum_px_ += frame_dirty_area_px_;
  dirty_rect_count_sum_ += frame_dirty_count_;
  ++dirty_frames_;

  RenderFrameSample sample;
  sample.sequence = total_frames_;
  sample.timestamp_ms = now_i;
  sample.frame_interval_ms = interval_ms;
  sample.width = frame_width_;
  sample.height = frame_height_;
  sample.dirty_count = frame_dirty_count_;
  sample.dirty_area_px = frame_dirty_area_px_;
  sample.buffer_bytes =
      static_cast<int64_t>(frame_width_) * frame_height_ * 4;
  sample.is_popup = frame_is_popup_;
  sample.stage_ms.fill(-1.0);
  sample.stage_ms[static_cast<int>(RenderStage::kOnPaint)] = onpaint_ms;
  sample.stage_ms[static_cast<int>(RenderStage::kSetViewImage)] = setview_ms_;

  PushFrameLocked(std::move(sample));

  last_onpaint_end_ms_ = now_ms;
  dispatch_pending_ = true;
  dispatch_ms_ = now_ms;
}

void RenderStats::OnPaintEventBegin() {
  if (!enabled_.load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> lock(mutex_);
  paint_event_active_ = true;
  paint_event_begin_ms_ = SteadyNowMsDouble();
  snapshot_observed_ = false;
  draw_observed_ = false;
  snapshot_ms_ = 0;
  draw_ms_ = 0;
  ++window_paint_events_;
  if (dispatch_pending_) {
    paint_schedule_ms_ = SteadyNowMsDouble() - dispatch_ms_;
    dispatch_pending_ = false;
    RecordStageStat(stage_aggs_[static_cast<int>(RenderStage::kScheduleDelay)],
                    paint_schedule_ms_);
  } else {
    paint_schedule_ms_ = -1.0;
  }
}

void RenderStats::OnSnapshotBegin() {
  if (!enabled_.load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_active_ = true;
  snapshot_begin_ms_ = SteadyNowMsDouble();
}

void RenderStats::OnSnapshotDone() {
  if (!enabled_.load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!snapshot_active_) return;
  snapshot_active_ = false;
  snapshot_ms_ = SteadyNowMsDouble() - snapshot_begin_ms_;
  snapshot_observed_ = true;
  RecordStageStat(stage_aggs_[static_cast<int>(RenderStage::kSnapshot)],
                  snapshot_ms_);
}

void RenderStats::OnDrawImageBegin() {
  if (!enabled_.load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> lock(mutex_);
  draw_active_ = true;
  draw_begin_ms_ = SteadyNowMsDouble();
}

void RenderStats::OnDrawImageDone() {
  if (!enabled_.load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!draw_active_) return;
  draw_active_ = false;
  draw_ms_ = SteadyNowMsDouble() - draw_begin_ms_;
  draw_observed_ = true;
  RecordStageStat(stage_aggs_[static_cast<int>(RenderStage::kDrawImage)],
                  draw_ms_);
}

void RenderStats::OnPaintEventEnd() {
  if (!enabled_.load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!paint_event_active_) return;
  paint_event_active_ = false;

  // 把 schedule/snapshot/draw 阶段写回最近样本。OnPaint 与 paintEvent 并非
  // 严格 1:1（Qt update() 会合并多次），此处近似归属到最新样本。
  if (ring_count_ > 0) {
    const size_t tail = (ring_head_ + ring_count_ - 1) % ring_.size();
    RenderFrameSample& sample = ring_[tail];
    sample.stage_ms[static_cast<int>(RenderStage::kScheduleDelay)] =
        paint_schedule_ms_;
    sample.stage_ms[static_cast<int>(RenderStage::kSnapshot)] =
        snapshot_observed_ ? snapshot_ms_ : -1.0;
    sample.stage_ms[static_cast<int>(RenderStage::kDrawImage)] =
        draw_observed_ ? draw_ms_ : -1.0;
  }

  snapshot_ms_ = 0;
  draw_ms_ = 0;
  paint_schedule_ms_ = -1.0;
}

RenderStatsSnapshot RenderStats::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return SnapshotLocked();
}

RenderStatsSnapshot RenderStats::SnapshotAndResetWindow() {
  std::lock_guard<std::mutex> lock(mutex_);
  RenderStatsSnapshot snapshot = SnapshotLocked();
  ResetWindowLocked(SteadyNowMsDouble());
  return snapshot;
}

RenderStatsSnapshot RenderStats::SnapshotLocked() const {
  RenderStatsSnapshot snapshot;
  snapshot.enabled = enabled_.load(std::memory_order_relaxed);
  snapshot.total_frames = total_frames_;
  snapshot.window_frames = window_frames_;
  snapshot.window_paint_events = window_paint_events_;
  if (window_frames_ > 0 || window_paint_events_ > 0) {
    snapshot.window_seconds =
        std::max(0.0, (SteadyNowMsDouble() - window_begin_ms_) / 1000.0);
  }
  snapshot.fps = (snapshot.window_seconds > 0)
                     ? static_cast<double>(window_frames_) /
                           snapshot.window_seconds
                     : 0.0;
  snapshot.frame_interval_ms = frame_interval_agg_;
  snapshot.stages = stage_aggs_;
  snapshot.last_width = last_width_;
  snapshot.last_height = last_height_;
  snapshot.last_dirty_count = last_dirty_count_;
  snapshot.last_dirty_area_px = last_dirty_area_px_;
  snapshot.dirty_area_sum_px = dirty_area_sum_px_;
  snapshot.dirty_rect_count_sum = dirty_rect_count_sum_;
  snapshot.dirty_frames = dirty_frames_;
  snapshot.recent_frames.reserve(ring_count_);
  for (size_t i = 0; i < ring_count_; ++i) {
    snapshot.recent_frames.push_back(ring_[(ring_head_ + i) % ring_.size()]);
  }
  return snapshot;
}

void RenderStats::PushFrameLocked(RenderFrameSample sample) {
  if (ring_.empty()) return;
  if (ring_count_ < ring_.size()) {
    ring_[(ring_head_ + ring_count_) % ring_.size()] = std::move(sample);
    ++ring_count_;
  } else {
    ring_[ring_head_] = std::move(sample);
    ring_head_ = (ring_head_ + 1) % ring_.size();
  }
}

uint64_t RenderStats::total_frames() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_frames_;
}

std::string FormatRenderStatsSummary(const RenderStatsSnapshot& snapshot) {
  static const char* const kStageNames[] = {
      "onpaint", "set_view_image", "schedule_delay", "snapshot", "draw_image"};
  constexpr std::size_t kStageCount = static_cast<std::size_t>(RenderStage::kCount);

  std::ostringstream stream;
  stream.setf(std::ios::fixed);
  stream << "RenderStats buf=" << snapshot.last_width << "x"
         << snapshot.last_height << " frames=" << snapshot.window_frames
         << " pe=" << snapshot.window_paint_events
         << " dt_ms=" << static_cast<int64_t>(snapshot.window_seconds * 1000.0)
         << " fps=" << std::setprecision(1) << snapshot.fps
         << " ival_avg=" << std::setprecision(2)
         << snapshot.frame_interval_ms.avg_ms() << " min="
         << snapshot.frame_interval_ms.min_ms << " max="
         << snapshot.frame_interval_ms.max_ms;
  for (std::size_t i = 0; i < kStageCount; ++i) {
    const RenderStageStats& stat = snapshot.stages[i];
    stream << " | " << kStageNames[i] << " avg=" << stat.avg_ms()
           << " min=" << stat.min_ms << " max=" << stat.max_ms
           << " last=" << stat.last_ms;
  }
  const double dirty_frames = static_cast<double>(snapshot.dirty_frames);
  stream << " | dirty_avg=" << std::setprecision(0)
         << (dirty_frames > 0 ? snapshot.dirty_area_sum_px / dirty_frames : 0.0)
         << " last=" << snapshot.last_dirty_area_px << " count_avg="
         << (dirty_frames > 0 ? snapshot.dirty_rect_count_sum / dirty_frames
                              : 0.0);
  return stream.str();
}

}  // namespace offscreen
