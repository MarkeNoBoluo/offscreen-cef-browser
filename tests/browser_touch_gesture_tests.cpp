#include "browser/browser_touch_gesture.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using offscreen::TouchGestureAction;
using offscreen::TouchGestureState;
using offscreen::TouchGestureStateMachine;
using offscreen::TouchPointSnapshot;

TouchPointSnapshot pressed(int id, int x, int y) {
  return TouchPointSnapshot{id, x, y, true};
}

TouchPointSnapshot released(int id, int x, int y) {
  return TouchPointSnapshot{id, x, y, false};
}

void expect_action(TouchGestureAction actual,
                   TouchGestureAction expected,
                   const char* label) {
  if (actual != expected) {
    std::cerr << label << " expected action [" << static_cast<int>(expected)
              << "] but got [" << static_cast<int>(actual) << "]\n";
    std::exit(1);
  }
}

void expect_state(TouchGestureState actual,
                  TouchGestureState expected,
                  const char* label) {
  if (actual != expected) {
    std::cerr << label << " expected state [" << static_cast<int>(expected)
              << "] but got [" << static_cast<int>(actual) << "]\n";
    std::exit(1);
  }
}

void expect_double(double actual, double expected, const char* label) {
  if (std::abs(actual - expected) > 0.0001) {
    std::cerr << label << " expected [" << expected << "] but got ["
              << actual << "]\n";
    std::exit(1);
  }
}

void begin_single_touch(TouchGestureStateMachine& machine) {
  expect_action(machine.Update({pressed(1, 0, 0)}, 0),
                TouchGestureAction::kNone, "single touch start");
}

void test_single_touch_enters_normal_swipe_at_15_dip() {
  TouchGestureStateMachine machine;
  begin_single_touch(machine);

  expect_action(machine.Update({pressed(1, 14, 0)}, 10),
                TouchGestureAction::kNone, "14 DIP action");
  expect_state(machine.state(), TouchGestureState::kSingleTouchCandidate,
               "14 DIP state");

  expect_action(machine.Update({pressed(1, 15, 0)}, 20),
                TouchGestureAction::kNone, "15 DIP action");
  expect_state(machine.state(), TouchGestureState::kNormalSwipe,
               "15 DIP state");
}

void test_equal_displacement_is_vertical_swipe() {
  TouchGestureStateMachine machine;
  begin_single_touch(machine);

  expect_action(machine.Update({pressed(1, 15, 15)}, 10),
                TouchGestureAction::kNone, "equal displacement action");
  expect_state(machine.state(), TouchGestureState::kNormalSwipe,
               "equal displacement state");
  expect_action(machine.Update({pressed(1, 80, 0)}, 20),
                TouchGestureAction::kNone, "equal displacement navigation");
}

void test_two_finger_distance_change_enters_zoom_at_20_dip() {
  TouchGestureStateMachine machine;
  begin_single_touch(machine);

  expect_action(machine.Update({pressed(1, 0, 0), pressed(2, 20, 0)}, 10),
                TouchGestureAction::kNone, "two finger start");
  expect_state(machine.state(), TouchGestureState::kTwoFingerScaleCandidate,
               "two finger candidate state");

  expect_action(machine.Update({pressed(1, 0, 0), pressed(2, 39, 0)}, 20),
                TouchGestureAction::kNone, "19 DIP scale action");
  expect_state(machine.state(), TouchGestureState::kTwoFingerScaleCandidate,
               "19 DIP scale state");
  expect_double(machine.scale_factor(), 1.0, "19 DIP scale factor");

  expect_action(machine.Update({pressed(1, 0, 0), pressed(2, 40, 0)}, 30),
                TouchGestureAction::kNone, "20 DIP scale action");
  expect_state(machine.state(), TouchGestureState::kTwoFingerZoom,
               "20 DIP scale state");
  expect_double(machine.scale_factor(), 2.0, "20 DIP scale factor");
}

void test_single_touch_never_enters_two_finger_zoom() {
  TouchGestureStateMachine machine;
  begin_single_touch(machine);

  expect_action(machine.Update({pressed(1, 0, 100)}, 10),
                TouchGestureAction::kNone, "single touch move action");
  expect_state(machine.state(), TouchGestureState::kNormalSwipe,
               "single touch move state");
}

void test_long_press_requires_500_ms_and_less_than_10_dip() {
  TouchGestureStateMachine machine;
  begin_single_touch(machine);

  expect_action(machine.Update({pressed(1, 9, 0)}, 499),
                TouchGestureAction::kNone, "499 ms action");
  expect_state(machine.state(), TouchGestureState::kSingleTouchCandidate,
               "499 ms state");

  expect_action(machine.Update({pressed(1, 9, 0)}, 500),
                TouchGestureAction::kNone, "500 ms action");
  expect_state(machine.state(), TouchGestureState::kLongPressCandidate,
               "500 ms state");
}

void test_10_dip_cancels_long_press_candidate() {
  TouchGestureStateMachine machine;
  begin_single_touch(machine);

  expect_action(machine.Update({pressed(1, 10, 0)}, 500),
                TouchGestureAction::kNone, "10 DIP long press action");
  expect_state(machine.state(), TouchGestureState::kSingleTouchCandidate,
               "10 DIP long press state");
}

void test_long_press_drag_starts_at_12_dip_and_ends_on_release() {
  TouchGestureStateMachine machine;
  begin_single_touch(machine);

  expect_action(machine.Update({pressed(1, 0, 0)}, 500),
                TouchGestureAction::kNone, "long press action");
  expect_state(machine.state(), TouchGestureState::kLongPressCandidate,
               "long press state");

  expect_action(machine.Update({pressed(1, 11, 0)}, 510),
                TouchGestureAction::kNone, "11 DIP drag action");
  expect_state(machine.state(), TouchGestureState::kLongPressCandidate,
               "11 DIP drag state");

  expect_action(machine.Update({pressed(1, 12, 0)}, 520),
                TouchGestureAction::kBeginDrag, "12 DIP drag action");
  expect_state(machine.state(), TouchGestureState::kLongPressDrag,
               "12 DIP drag state");

  expect_action(machine.Update({pressed(1, 13, 0)}, 530),
                TouchGestureAction::kUpdateDrag, "drag update action");
  expect_action(machine.Update({released(1, 13, 0)}, 540),
                TouchGestureAction::kEndDrag, "drag release action");
  expect_state(machine.state(), TouchGestureState::kIdle, "drag release state");
}

void test_pre_long_press_move_uses_normal_swipe() {
  TouchGestureStateMachine machine;
  begin_single_touch(machine);

  expect_action(machine.Update({pressed(1, 15, 0)}, 100),
                TouchGestureAction::kNone, "pre-long-press move action");
  expect_state(machine.state(), TouchGestureState::kNormalSwipe,
               "pre-long-press move state");

  expect_action(machine.Update({pressed(1, 20, 0)}, 500),
                TouchGestureAction::kNone, "post-timeout swipe action");
  expect_state(machine.state(), TouchGestureState::kNormalSwipe,
               "post-timeout swipe state");
}

void test_navigation_requires_80_horizontal_dip_and_under_30_vertical_dip() {
  TouchGestureStateMachine machine;
  begin_single_touch(machine);

  expect_action(machine.Update({pressed(1, 79, 0)}, 10),
                TouchGestureAction::kNone, "79 DIP navigation action");
  expect_action(machine.Update({pressed(1, 80, 29)}, 20),
                TouchGestureAction::kBack, "80/29 DIP navigation action");
}

void test_navigation_rejects_30_vertical_dip() {
  TouchGestureStateMachine machine;
  begin_single_touch(machine);

  expect_action(machine.Update({pressed(1, 80, 30)}, 10),
                TouchGestureAction::kNone, "80/30 DIP navigation action");
}

void test_navigation_direction_and_single_action_limit() {
  TouchGestureStateMachine machine;
  begin_single_touch(machine);

  expect_action(machine.Update({pressed(1, -80, 0)}, 10),
                TouchGestureAction::kForward, "right-to-left action");
  expect_action(machine.Update({pressed(1, -100, 0)}, 20),
                TouchGestureAction::kNone, "second navigation action");
}

void test_cancel_and_lost_touch_reset_the_sequence() {
  TouchGestureStateMachine machine;
  begin_single_touch(machine);

  expect_action(machine.Cancel(), TouchGestureAction::kCancel, "cancel action");
  expect_state(machine.state(), TouchGestureState::kCancelled, "cancel state");

  expect_action(machine.Update({pressed(1, 0, 0)}, 10),
                TouchGestureAction::kNone, "restart action");
  expect_action(machine.Update({}, 20), TouchGestureAction::kCancel,
                "lost touch action");
  expect_state(machine.state(), TouchGestureState::kCancelled,
               "lost touch state");
}

void test_lost_second_touch_cancels_two_finger_sequence() {
  TouchGestureStateMachine machine;
  begin_single_touch(machine);

  expect_action(machine.Update({pressed(1, 0, 0), pressed(2, 20, 0)}, 10),
                TouchGestureAction::kNone, "two finger start action");
  expect_action(machine.Update({pressed(1, 0, 0)}, 20),
                TouchGestureAction::kCancel, "lost second touch action");
  expect_state(machine.state(), TouchGestureState::kCancelled,
               "lost second touch state");
}

}  // namespace

int main() {
  test_single_touch_enters_normal_swipe_at_15_dip();
  test_equal_displacement_is_vertical_swipe();
  test_two_finger_distance_change_enters_zoom_at_20_dip();
  test_single_touch_never_enters_two_finger_zoom();
  test_long_press_requires_500_ms_and_less_than_10_dip();
  test_10_dip_cancels_long_press_candidate();
  test_long_press_drag_starts_at_12_dip_and_ends_on_release();
  test_pre_long_press_move_uses_normal_swipe();
  test_navigation_requires_80_horizontal_dip_and_under_30_vertical_dip();
  test_navigation_rejects_30_vertical_dip();
  test_navigation_direction_and_single_action_limit();
  test_cancel_and_lost_touch_reset_the_sequence();
  test_lost_second_touch_cancels_two_finger_sequence();
  return 0;
}
