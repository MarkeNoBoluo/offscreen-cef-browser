#pragma once

#include <cstdint>
#include <vector>

namespace offscreen {

struct TouchPointSnapshot {
  int id = 0;
  int x = 0;
  int y = 0;
  bool pressed = false;
};

enum class TouchGestureAction {
  kNone,
  kForward,
  kBack,
  kBeginDrag,
  kUpdateDrag,
  kEndDrag,
  kCancel,
};

enum class TouchGestureState {
  kIdle,
  kSingleTouchCandidate,
  kNormalSwipe,
  kTwoFingerScaleCandidate,
  kTwoFingerZoom,
  kLongPressCandidate,
  kLongPressDrag,
  kCancelled,
};

struct TouchGestureThresholds {
  int swipe_start_dip = 15;
  int scale_start_dip = 20;
  int long_press_duration_ms = 500;
  int long_press_max_movement_dip = 10;
  int drag_start_dip = 12;
  int navigation_horizontal_dip = 80;
  int navigation_vertical_max_dip = 30;
  double minimum_scale = 0.5;
  double maximum_scale = 3.0;
};

class TouchGestureStateMachine {
 public:
  explicit TouchGestureStateMachine(
      TouchGestureThresholds thresholds = TouchGestureThresholds{});

  TouchGestureAction Update(const std::vector<TouchPointSnapshot>& points,
                            int64_t timestamp_ms);
  TouchGestureAction Cancel();

  TouchGestureState state() const;
  double scale_factor() const;

 private:
  void BeginSingleTouch(const TouchPointSnapshot& point, int64_t timestamp_ms);
  void BeginTwoFingerScale(const TouchPointSnapshot& first,
                           const TouchPointSnapshot& second);
  void ResetToIdle();

  TouchGestureThresholds thresholds_;
  TouchGestureState state_ = TouchGestureState::kIdle;
  int primary_id_ = 0;
  int start_x_ = 0;
  int start_y_ = 0;
  int recent_x_ = 0;
  int recent_y_ = 0;
  int64_t start_timestamp_ms_ = 0;
  double initial_two_finger_distance_ = 0.0;
  double scale_factor_ = 1.0;
  bool long_press_eligible_ = false;
  bool horizontal_swipe_ = false;
  bool navigation_sent_ = false;
};

}  // namespace offscreen
