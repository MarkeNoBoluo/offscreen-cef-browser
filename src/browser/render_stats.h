#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace offscreen {

// OSR 渲染链性能采集的命名阶段。枚举值顺序必须与
// RenderFrameSample::stage_ms / RenderStatsSnapshot::stages 的数组索引一致；
// 新增阶段追加在 kCount 之前，并同步各采集点与 FormatRenderStatsSummary。
enum class RenderStage : uint8_t {
  kOnPaint = 0,
  kSetViewImage,
  kScheduleDelay,
  kSnapshot,
  kDrawImage,
  kCount
};

// 单阶段聚合（增量维护 count/sum/min/max/last，导出时计算 avg）。
struct RenderStageStats {
  uint64_t count = 0;
  double min_ms = 0;
  double max_ms = 0;
  double sum_ms = 0;
  double last_ms = 0;

  /// 平均耗时（毫秒）；无观测时为 0。
  double avg_ms() const {
    return count ? sum_ms / static_cast<double>(count) : 0.0;
  }
};

// 单帧详细记录（环形缓冲元素；纯 STL + 数值类型，moc 安全）。
struct RenderFrameSample {
  uint64_t sequence = 0;
  int64_t timestamp_ms = 0;  // steady_clock 单调毫秒
  double frame_interval_ms = 0;
  int width = 0;
  int height = 0;
  int dirty_count = 0;
  int64_t dirty_area_px = 0;
  int64_t buffer_bytes = 0;  // width*height*4
  bool is_popup = false;
  std::array<double, 5> stage_ms{};  // -1.0 = 该帧未观测到该阶段
};

// 周期快照（日志与信号的载荷）。
struct RenderStatsSnapshot {
  bool enabled = false;
  uint64_t total_frames = 0;  // 生命周期累计（不回零）
  uint64_t window_frames = 0;  // 窗口内 OnPaint 帧数
  uint64_t window_paint_events = 0;
  double window_seconds = 0;  // steady_clock 实算，非定时器 tick
  double fps = 0;             // window_frames / window_seconds
  RenderStageStats frame_interval_ms;
  std::array<RenderStageStats, 5> stages{};
  int last_width = 0;
  int last_height = 0;
  int last_dirty_count = 0;
  int64_t last_dirty_area_px = 0;
  // 窗口内脏区聚合（供 summary 输出 dirty_avg / count_avg）。
  int64_t dirty_area_sum_px = 0;
  int64_t dirty_rect_count_sum = 0;
  uint64_t dirty_frames = 0;
  std::vector<RenderFrameSample> recent_frames;  // 最近 N 帧，时间升序
};

/// 将渲染统计快照格式化为单行文本。
/// @param snapshot 要格式化的快照。
/// @return 单行 UTF-8 文本。
std::string FormatRenderStatsSummary(const RenderStatsSnapshot& snapshot);

// 渲染链路性能统计核心：采集 FPS、各阶段耗时、buffer 尺寸与脏区面积。
// 纯 STL、线程安全；采集点方法在禁用时首行近零成本 early-return。
// 锁内只做微秒级算术，采集器绝不持有锁调用 frame_->Snapshot()/SetViewImage()。
class RenderStats {
 public:
  static constexpr std::size_t kDefaultRingCapacity = 120;

  /// 创建统计核心。
  /// @param ring_capacity 最近帧环形缓冲容量；至少为 1。
  explicit RenderStats(std::size_t ring_capacity = kDefaultRingCapacity);
  ~RenderStats() = default;

  RenderStats(const RenderStats&) = delete;
  RenderStats& operator=(const RenderStats&) = delete;

  /// 开启采集并重置窗口聚合。
  void Enable();
  /// 关闭采集：各采集点近零成本 no-op。
  void Disable();
  /// 查询采集是否开启。
  bool enabled() const;

  // CEF 侧采集点（OsrRenderHandler::OnPaint）。
  /// 开始记录一帧 OnPaint，并携带该帧元数据。
  /// @param width 物理像素宽度。
  /// @param height 物理像素高度。
  /// @param dirty_count 脏区域数量。
  /// @param dirty_area_px 脏区总面积（物理像素）。
  /// @param is_popup 是否为弹出层帧。
  void OnPaintBegin(int width, int height, int dirty_count,
                    int64_t dirty_area_px, bool is_popup);
  /// 开始计时 SetViewImage/SetPopupImage（拷贝 #1）。
  void OnSetViewImageBegin();
  /// 结束计时 SetViewImage/SetPopupImage。
  void OnSetViewImageDone();
  /// 结束一帧 OnPaint：入环、记录 dispatch 时刻并聚合阶段统计。
  void OnPaintEnd();

  // Qt 侧采集点（BrowserWidget::paintEvent）。
  /// 开始记录一次 paintEvent，并计算 OnPaint→paintEvent 排队延迟。
  void OnPaintEventBegin();
  /// 开始计时 Snapshot（拷贝 #2）。
  void OnSnapshotBegin();
  /// 结束计时 Snapshot。
  void OnSnapshotDone();
  /// 开始计时 drawImage。
  void OnDrawImageBegin();
  /// 结束计时 drawImage。
  void OnDrawImageDone();
  /// 结束一次 paintEvent：把 schedule/snapshot/draw 阶段写回最近样本。
  void OnPaintEventEnd();

  /// 导出当前快照（不重置窗口聚合）。
  RenderStatsSnapshot Snapshot() const;
  /// 导出当前快照并重置窗口聚合（供周期定时器调用）。
  RenderStatsSnapshot SnapshotAndResetWindow();
  /// 生命周期累计帧数。
  uint64_t total_frames() const;

 private:
  /// 假设 mutex_ 已持有，导出当前快照。
  RenderStatsSnapshot SnapshotLocked() const;
  /// 假设 mutex_ 已持有，重置窗口聚合。
  /// @param now_ms 当前 steady_clock 毫秒，作为新窗口起点。
  void ResetWindowLocked(double now_ms);
  /// 假设 mutex_ 已持有，将样本推入环形缓冲。
  void PushFrameLocked(RenderFrameSample sample);

  mutable std::mutex mutex_;
  std::atomic<bool> enabled_{true};

  std::vector<RenderFrameSample> ring_;
  size_t ring_head_ = 0;
  size_t ring_count_ = 0;

  // 当前 OnPaint 帧构建状态。
  bool frame_build_active_ = false;
  double onpaint_begin_ms_ = 0;
  bool setview_active_ = false;
  double setview_begin_ms_ = 0;
  double setview_ms_ = 0;
  int frame_width_ = 0;
  int frame_height_ = 0;
  int frame_dirty_count_ = 0;
  int64_t frame_dirty_area_px_ = 0;
  bool frame_is_popup_ = false;

  // 当前 paintEvent 构建状态。
  bool paint_event_active_ = false;
  double paint_event_begin_ms_ = 0;
  bool snapshot_active_ = false;
  double snapshot_begin_ms_ = 0;
  double snapshot_ms_ = 0;
  bool snapshot_observed_ = false;
  bool draw_active_ = false;
  double draw_begin_ms_ = 0;
  double draw_ms_ = 0;
  bool draw_observed_ = false;
  bool dispatch_pending_ = false;
  double dispatch_ms_ = 0;
  double paint_schedule_ms_ = -1.0;

  // 生命周期与窗口聚合。
  uint64_t total_frames_ = 0;
  uint64_t window_frames_ = 0;
  uint64_t window_paint_events_ = 0;
  double window_begin_ms_ = 0;
  double last_onpaint_end_ms_ = 0;
  RenderStageStats frame_interval_agg_;
  std::array<RenderStageStats, 5> stage_aggs_{};
  int last_width_ = 0;
  int last_height_ = 0;
  int last_dirty_count_ = 0;
  int64_t last_dirty_area_px_ = 0;
  int64_t dirty_area_sum_px_ = 0;
  int64_t dirty_rect_count_sum_ = 0;
  uint64_t dirty_frames_ = 0;
};

}  // namespace offscreen
