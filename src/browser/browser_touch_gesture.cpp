#include "browser/browser_touch_gesture.h"

#include <algorithm>
#include <cmath>

namespace offscreen {

namespace {

const TouchPointSnapshot* FindPoint(
    const std::vector<TouchPointSnapshot>& points,
    int id) {
  for (const TouchPointSnapshot& point : points) {
    if (point.id == id) {
      return &point;
    }
  }
  return nullptr;
}

const TouchPointSnapshot* FindSecondPressedPoint(
    const std::vector<TouchPointSnapshot>& points,
    int primary_id) {
  for (const TouchPointSnapshot& point : points) {
    if (point.pressed && point.id != primary_id) {
      return &point;
    }
  }
  return nullptr;
}

double Distance(const TouchPointSnapshot& first,
                const TouchPointSnapshot& second) {
  return std::hypot(static_cast<double>(second.x - first.x),
                    static_cast<double>(second.y - first.y));
}

}  // namespace

TouchGestureStateMachine::TouchGestureStateMachine(
    TouchGestureThresholds thresholds)
    : thresholds_(thresholds) {}

TouchGestureAction TouchGestureStateMachine::Update(
    const std::vector<TouchPointSnapshot>& points,
    int64_t timestamp_ms) {
  const TouchPointSnapshot* first_pressed = nullptr;
  for (const TouchPointSnapshot& point : points) {
    if (point.pressed) {
      first_pressed = &point;
      break;
    }
  }

  if (state_ == TouchGestureState::kIdle ||
      state_ == TouchGestureState::kCancelled) {
    if (first_pressed == nullptr) {
      return TouchGestureAction::kNone;
    }
    BeginSingleTouch(*first_pressed, timestamp_ms);
  }

  const TouchPointSnapshot* primary = FindPoint(points, primary_id_);
  if (primary == nullptr || !primary->pressed) {
    if (primary != nullptr && !primary->pressed) {
      const bool was_dragging = state_ == TouchGestureState::kLongPressDrag;
      ResetToIdle();
      return was_dragging ? TouchGestureAction::kEndDrag
                          : TouchGestureAction::kNone;
    }
    state_ = TouchGestureState::kCancelled;
    return TouchGestureAction::kCancel;
  }

  const TouchPointSnapshot* second = FindSecondPressedPoint(points, primary_id_);
  if (second != nullptr) {
    if (state_ != TouchGestureState::kTwoFingerScaleCandidate &&
        state_ != TouchGestureState::kTwoFingerZoom) {
      BeginTwoFingerScale(*primary, *second);
      return TouchGestureAction::kNone;
    }

    const double distance = Distance(*primary, *second);
    if (state_ == TouchGestureState::kTwoFingerScaleCandidate &&
        std::abs(distance - initial_two_finger_distance_) >=
            thresholds_.scale_start_dip) {
      state_ = TouchGestureState::kTwoFingerZoom;
    }
    if (state_ == TouchGestureState::kTwoFingerZoom &&
        initial_two_finger_distance_ > 0.0) {
      scale_factor_ = std::clamp(distance / initial_two_finger_distance_,
                                 thresholds_.minimum_scale,
                                 thresholds_.maximum_scale);
    }
    return TouchGestureAction::kNone;
  }

  if (state_ == TouchGestureState::kTwoFingerScaleCandidate ||
      state_ == TouchGestureState::kTwoFingerZoom) {
    state_ = TouchGestureState::kCancelled;
    return TouchGestureAction::kCancel;
  }

  const int delta_x = primary->x - start_x_;
  const int delta_y = primary->y - start_y_;
  const double movement = std::hypot(static_cast<double>(delta_x),
                                     static_cast<double>(delta_y));

  if (state_ == TouchGestureState::kSingleTouchCandidate) {
    if (movement >= thresholds_.swipe_start_dip) {
      state_ = TouchGestureState::kNormalSwipe;
      long_press_eligible_ = false;
      horizontal_swipe_ = std::abs(delta_x) > std::abs(delta_y);
    } else if (movement >= thresholds_.long_press_max_movement_dip) {
      long_press_eligible_ = false;
    }

    if (state_ == TouchGestureState::kSingleTouchCandidate &&
        long_press_eligible_ &&
        timestamp_ms - start_timestamp_ms_ >=
            thresholds_.long_press_duration_ms) {
      state_ = TouchGestureState::kLongPressCandidate;
    }
  }

  if (state_ == TouchGestureState::kLongPressCandidate) {
    if (movement >= thresholds_.drag_start_dip) {
      state_ = TouchGestureState::kLongPressDrag;
      recent_x_ = primary->x;
      recent_y_ = primary->y;
      return TouchGestureAction::kBeginDrag;
    }
    recent_x_ = primary->x;
    recent_y_ = primary->y;
    return TouchGestureAction::kNone;
  }

  if (state_ == TouchGestureState::kLongPressDrag) {
    const bool moved = primary->x != recent_x_ || primary->y != recent_y_;
    recent_x_ = primary->x;
    recent_y_ = primary->y;
    return moved ? TouchGestureAction::kUpdateDrag : TouchGestureAction::kNone;
  }

  if (state_ == TouchGestureState::kNormalSwipe && !navigation_sent_ &&
      horizontal_swipe_ &&
      std::abs(delta_y) < thresholds_.navigation_vertical_max_dip &&
      std::abs(delta_x) >= thresholds_.navigation_horizontal_dip) {
    navigation_sent_ = true;
    return delta_x > 0 ? TouchGestureAction::kBack
                       : TouchGestureAction::kForward;
  }

  recent_x_ = primary->x;
  recent_y_ = primary->y;
  return TouchGestureAction::kNone;
}

TouchGestureAction TouchGestureStateMachine::Cancel() {
  if (state_ == TouchGestureState::kIdle ||
      state_ == TouchGestureState::kCancelled) {
    return TouchGestureAction::kNone;
  }
  state_ = TouchGestureState::kCancelled;
  return TouchGestureAction::kCancel;
}

TouchGestureState TouchGestureStateMachine::state() const {
  return state_;
}

double TouchGestureStateMachine::scale_factor() const {
  return scale_factor_;
}

void TouchGestureStateMachine::BeginSingleTouch(
    const TouchPointSnapshot& point,
    int64_t timestamp_ms) {
  state_ = TouchGestureState::kSingleTouchCandidate;
  primary_id_ = point.id;
  start_x_ = point.x;
  start_y_ = point.y;
  recent_x_ = point.x;
  recent_y_ = point.y;
  start_timestamp_ms_ = timestamp_ms;
  initial_two_finger_distance_ = 0.0;
  scale_factor_ = 1.0;
  long_press_eligible_ = true;
  horizontal_swipe_ = false;
  navigation_sent_ = false;
}

void TouchGestureStateMachine::BeginTwoFingerScale(
    const TouchPointSnapshot& first,
    const TouchPointSnapshot& second) {
  state_ = TouchGestureState::kTwoFingerScaleCandidate;
  initial_two_finger_distance_ = Distance(first, second);
  scale_factor_ = 1.0;
  long_press_eligible_ = false;
  horizontal_swipe_ = false;
  navigation_sent_ = false;
}

void TouchGestureStateMachine::ResetToIdle() {
  state_ = TouchGestureState::kIdle;
  primary_id_ = 0;
  initial_two_finger_distance_ = 0.0;
  scale_factor_ = 1.0;
  long_press_eligible_ = false;
  horizontal_swipe_ = false;
  navigation_sent_ = false;
}

}  // namespace offscreen
