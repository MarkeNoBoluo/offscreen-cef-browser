#pragma once

#include <cstdint>

namespace offscreen {

// CEF event flag constants (values match cef_event_flags_t in CEF 96)
constexpr uint32_t kEventFlagNone = 0;
constexpr uint32_t kEventFlagCapsLockOn = 1 << 0;
constexpr uint32_t kEventFlagShiftDown = 1 << 1;
constexpr uint32_t kEventFlagControlDown = 1 << 2;
constexpr uint32_t kEventFlagAltDown = 1 << 3;
constexpr uint32_t kEventFlagLeftButton = 1 << 4;
constexpr uint32_t kEventFlagMiddleButton = 1 << 5;
constexpr uint32_t kEventFlagRightButton = 1 << 6;
constexpr uint32_t kEventFlagCommandDown = 1 << 7;
constexpr uint32_t kEventFlagNumLockOn = 1 << 8;
constexpr uint32_t kEventFlagIsKeyPad = 1 << 9;
constexpr uint32_t kEventFlagIsLeft = 1 << 10;
constexpr uint32_t kEventFlagIsRight = 1 << 11;
constexpr uint32_t kEventFlagAltGrDown = 1 << 12;
constexpr uint32_t kEventFlagIsRepeat = 1 << 13;

// CEF mouse button type constants (values match cef_mouse_button_type_t)
constexpr int kMouseButtonLeft = 0;    // MBT_LEFT
constexpr int kMouseButtonMiddle = 1;  // MBT_MIDDLE
constexpr int kMouseButtonRight = 2;   // MBT_RIGHT

uint32_t MapQtModifiersToCefEventFlags(int qt_modifiers);
int MapQtMouseButtonToCefMouseButton(int qt_mouse_button);
uint32_t MouseButtonsToCefEventFlags(int qt_mouse_buttons);
uint32_t MapWindowsKeyboardMessageToCefEventFlags(uintptr_t w_param,
                                                  intptr_t l_param);
bool IsWindowsKeyDown(int windows_key_code);
bool IsExtendedWindowsVirtualKey(int windows_key_code);
uint32_t BuildWindowsNativeKeyCode(int windows_key_code,
                                   int native_scan_code,
                                   bool was_key_down,
                                   bool is_key_up);

}  // namespace offscreen
