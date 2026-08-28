#include "browser/browser_input_mapping.h"

#include <cstdlib>
#include <iostream>

#include <windows.h>

namespace {

// Qt modifier constants matching those in browser_input_mapping.cpp
constexpr int kQtShiftModifier = 0x02000000;
constexpr int kQtControlModifier = 0x04000000;
constexpr int kQtAltModifier = 0x08000000;
constexpr int kQtMetaModifier = 0x10000000;
constexpr int kQtKeypadModifier = 0x20000000;

constexpr int kQtLeftButton = 0x00000001;
constexpr int kQtRightButton = 0x00000002;
constexpr int kQtMiddleButton = 0x00000004;

void expect_eq(uint32_t actual, uint32_t expected, const char* label) {
  if (actual != expected) {
    std::cerr << label << " expected [" << expected << "] but got [" << actual
              << "]\n";
    std::exit(1);
  }
}

void expect_eq_int(int actual, int expected, const char* label) {
  if (actual != expected) {
    std::cerr << label << " expected [" << expected << "] but got [" << actual
              << "]\n";
    std::exit(1);
  }
}

void expect_eq_bool(bool actual, bool expected, const char* label) {
  if (actual != expected) {
    std::cerr << label << " expected [" << expected << "] but got [" << actual
              << "]\n";
    std::exit(1);
  }
}

// --- MapQtModifiersToCefEventFlags tests ---

void test_modifiers_none() {
  const uint32_t result = offscreen::MapQtModifiersToCefEventFlags(0);
  expect_eq(result, offscreen::kEventFlagNone, "modifiers_none");
}

void test_modifiers_shift() {
  const uint32_t result =
      offscreen::MapQtModifiersToCefEventFlags(kQtShiftModifier);
  expect_eq(result, offscreen::kEventFlagShiftDown, "modifiers_shift");
}

void test_modifiers_ctrl() {
  const uint32_t result =
      offscreen::MapQtModifiersToCefEventFlags(kQtControlModifier);
  expect_eq(result, offscreen::kEventFlagControlDown, "modifiers_ctrl");
}

void test_modifiers_alt() {
  const uint32_t result =
      offscreen::MapQtModifiersToCefEventFlags(kQtAltModifier);
  expect_eq(result, offscreen::kEventFlagAltDown, "modifiers_alt");
}

void test_modifiers_meta() {
  const uint32_t result =
      offscreen::MapQtModifiersToCefEventFlags(kQtMetaModifier);
  expect_eq(result, offscreen::kEventFlagCommandDown, "modifiers_meta");
}

void test_modifiers_keypad() {
  const uint32_t result =
      offscreen::MapQtModifiersToCefEventFlags(kQtKeypadModifier);
  expect_eq(result, offscreen::kEventFlagIsKeyPad, "modifiers_keypad");
}

void test_modifiers_shift_ctrl() {
  const uint32_t result = offscreen::MapQtModifiersToCefEventFlags(
      kQtShiftModifier | kQtControlModifier);
  expect_eq(result,
            offscreen::kEventFlagShiftDown | offscreen::kEventFlagControlDown,
            "modifiers_shift_ctrl");
}

void test_modifiers_all() {
  const uint32_t result = offscreen::MapQtModifiersToCefEventFlags(
      kQtShiftModifier | kQtControlModifier | kQtAltModifier | kQtMetaModifier |
      kQtKeypadModifier);
  const uint32_t expected = offscreen::kEventFlagShiftDown |
                            offscreen::kEventFlagControlDown |
                            offscreen::kEventFlagAltDown |
                            offscreen::kEventFlagCommandDown |
                            offscreen::kEventFlagIsKeyPad;
  expect_eq(result, expected, "modifiers_all");
}

void test_modifiers_unknown_bit_ignored() {
  constexpr int kUnknownBit = 0x00100000;
  const uint32_t result =
      offscreen::MapQtModifiersToCefEventFlags(kUnknownBit);
  expect_eq(result, offscreen::kEventFlagNone,
            "modifiers_unknown_bit_ignored");
}

// --- MapQtMouseButtonToCefMouseButton tests ---

void test_mouse_button_left() {
  const int result = offscreen::MapQtMouseButtonToCefMouseButton(kQtLeftButton);
  expect_eq_int(result, offscreen::kMouseButtonLeft, "mouse_button_left");
}

void test_mouse_button_right() {
  const int result =
      offscreen::MapQtMouseButtonToCefMouseButton(kQtRightButton);
  expect_eq_int(result, offscreen::kMouseButtonRight, "mouse_button_right");
}

void test_mouse_button_middle() {
  const int result =
      offscreen::MapQtMouseButtonToCefMouseButton(kQtMiddleButton);
  expect_eq_int(result, offscreen::kMouseButtonMiddle, "mouse_button_middle");
}

void test_mouse_button_unknown_falls_back_to_left() {
  const int result = offscreen::MapQtMouseButtonToCefMouseButton(0x00000010);
  expect_eq_int(result, offscreen::kMouseButtonLeft,
                "mouse_button_unknown_fallback");
}

// --- MouseButtonsToCefEventFlags tests ---

void test_mouse_buttons_none() {
  const uint32_t result = offscreen::MouseButtonsToCefEventFlags(0);
  expect_eq(result, offscreen::kEventFlagNone, "mouse_buttons_none");
}

void test_mouse_buttons_left() {
  const uint32_t result =
      offscreen::MouseButtonsToCefEventFlags(kQtLeftButton);
  expect_eq(result, offscreen::kEventFlagLeftButton, "mouse_buttons_left");
}

void test_mouse_buttons_right() {
  const uint32_t result =
      offscreen::MouseButtonsToCefEventFlags(kQtRightButton);
  expect_eq(result, offscreen::kEventFlagRightButton, "mouse_buttons_right");
}

void test_mouse_buttons_middle() {
  const uint32_t result =
      offscreen::MouseButtonsToCefEventFlags(kQtMiddleButton);
  expect_eq(result, offscreen::kEventFlagMiddleButton, "mouse_buttons_middle");
}

void test_mouse_buttons_left_and_right() {
  const uint32_t result =
      offscreen::MouseButtonsToCefEventFlags(kQtLeftButton | kQtRightButton);
  const uint32_t expected =
      offscreen::kEventFlagLeftButton | offscreen::kEventFlagRightButton;
  expect_eq(result, expected, "mouse_buttons_left_right");
}

void test_mouse_buttons_all_three() {
  const uint32_t result = offscreen::MouseButtonsToCefEventFlags(
      kQtLeftButton | kQtRightButton | kQtMiddleButton);
  const uint32_t expected = offscreen::kEventFlagLeftButton |
                            offscreen::kEventFlagRightButton |
                            offscreen::kEventFlagMiddleButton;
  expect_eq(result, expected, "mouse_buttons_all");
}

// --- Windows key native code tests ---

void test_extended_virtual_key_false_for_letters() {
  expect_eq_bool(offscreen::IsExtendedWindowsVirtualKey('A'), false,
                 "extended_virtual_key_letter");
}

void test_extended_virtual_key_true_for_arrow() {
  expect_eq_bool(offscreen::IsExtendedWindowsVirtualKey(VK_LEFT), true,
                 "extended_virtual_key_left_arrow");
}

void test_windows_native_key_code_keydown() {
  const uint32_t result =
      offscreen::BuildWindowsNativeKeyCode('A', 0x1e, false, false);
  expect_eq(result, 0x001e0001u, "windows_native_key_code_keydown");
}

void test_windows_native_key_code_keyup_sets_release_bits() {
  const uint32_t result =
      offscreen::BuildWindowsNativeKeyCode('A', 0x1e, true, true);
  expect_eq(result, 0xc01e0001u, "windows_native_key_code_keyup");
}

void test_windows_native_key_code_extended_key() {
  const uint32_t result =
      offscreen::BuildWindowsNativeKeyCode(VK_LEFT, 0x4b, false, false);
  expect_eq(result, 0x014b0001u, "windows_native_key_code_extended_key");
}

void test_windows_native_key_code_falls_back_to_virtual_key_scan_code() {
  const uint32_t result =
      offscreen::BuildWindowsNativeKeyCode('A', 0, false, false);
  expect_eq(result, 0x001e0001u,
            "windows_native_key_code_fallback_scan_code");
}

}  // namespace

int main() {
  // MapQtModifiersToCefEventFlags
  test_modifiers_none();
  test_modifiers_shift();
  test_modifiers_ctrl();
  test_modifiers_alt();
  test_modifiers_meta();
  test_modifiers_keypad();
  test_modifiers_shift_ctrl();
  test_modifiers_all();
  test_modifiers_unknown_bit_ignored();

  // MapQtMouseButtonToCefMouseButton
  test_mouse_button_left();
  test_mouse_button_right();
  test_mouse_button_middle();
  test_mouse_button_unknown_falls_back_to_left();

  // MouseButtonsToCefEventFlags
  test_mouse_buttons_none();
  test_mouse_buttons_left();
  test_mouse_buttons_right();
  test_mouse_buttons_middle();
  test_mouse_buttons_left_and_right();
  test_mouse_buttons_all_three();

  // Windows key native code
  test_extended_virtual_key_false_for_letters();
  test_extended_virtual_key_true_for_arrow();
  test_windows_native_key_code_keydown();
  test_windows_native_key_code_keyup_sets_release_bits();
  test_windows_native_key_code_extended_key();
  test_windows_native_key_code_falls_back_to_virtual_key_scan_code();

  return 0;
}
