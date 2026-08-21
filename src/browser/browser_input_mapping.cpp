#include "browser/browser_input_mapping.h"

#include <windows.h>

namespace offscreen {

namespace {

// Qt::KeyboardModifier bit values (Qt 5, from Qt::KeyboardModifiers enum)
constexpr int kQtShiftModifier = 0x02000000;
constexpr int kQtControlModifier = 0x04000000;
constexpr int kQtAltModifier = 0x08000000;
constexpr int kQtMetaModifier = 0x10000000;
constexpr int kQtKeypadModifier = 0x20000000;

// Qt::MouseButton values (Qt 5, from Qt::MouseButtons enum)
constexpr int kQtLeftButton = 0x00000001;
constexpr int kQtRightButton = 0x00000002;
constexpr int kQtMiddleButton = 0x00000004;

constexpr int kQtMouseEventNotSynthesized = 0;

constexpr uint32_t kWindowsKeyRepeatCount = 1;
constexpr uint32_t kWindowsKeyScanCodeShift = 16;
constexpr uint32_t kWindowsKeyExtendedFlag = 1u << 24;
constexpr uint32_t kWindowsKeyPreviousDownFlag = 1u << 30;
constexpr uint32_t kWindowsKeyTransitionUpFlag = 1u << 31;

}  // namespace

uint32_t MapQtModifiersToCefEventFlags(int qt_modifiers) {
  uint32_t flags = kEventFlagNone;
  if (qt_modifiers & kQtShiftModifier) {
    flags |= kEventFlagShiftDown;
  }
  if (qt_modifiers & kQtControlModifier) {
    flags |= kEventFlagControlDown;
  }
  if (qt_modifiers & kQtAltModifier) {
    flags |= kEventFlagAltDown;
  }
  if (qt_modifiers & kQtMetaModifier) {
    flags |= kEventFlagCommandDown;
  }
  if (qt_modifiers & kQtKeypadModifier) {
    flags |= kEventFlagIsKeyPad;
  }
  return flags;
}

int MapQtMouseButtonToCefMouseButton(int qt_mouse_button) {
  switch (qt_mouse_button) {
    case kQtLeftButton:
      return kMouseButtonLeft;
    case kQtRightButton:
      return kMouseButtonRight;
    case kQtMiddleButton:
      return kMouseButtonMiddle;
    default:
      return kMouseButtonLeft;
  }
}

uint32_t MouseButtonsToCefEventFlags(int qt_mouse_buttons) {
  uint32_t flags = kEventFlagNone;
  if (qt_mouse_buttons & kQtLeftButton) {
    flags |= kEventFlagLeftButton;
  }
  if (qt_mouse_buttons & kQtRightButton) {
    flags |= kEventFlagRightButton;
  }
  if (qt_mouse_buttons & kQtMiddleButton) {
    flags |= kEventFlagMiddleButton;
  }
  return flags;
}

bool ShouldForwardQtMouseEvent(int qt_mouse_event_source) {
  return qt_mouse_event_source == kQtMouseEventNotSynthesized;
}

bool IsWindowsKeyDown(int windows_key_code) {
  return (::GetKeyState(windows_key_code) & 0x8000) != 0;
}

uint32_t MapWindowsKeyboardMessageToCefEventFlags(uintptr_t w_param,
                                                  intptr_t l_param) {
  uint32_t flags = kEventFlagNone;
  if (IsWindowsKeyDown(VK_SHIFT)) {
    flags |= kEventFlagShiftDown;
  }
  if (IsWindowsKeyDown(VK_CONTROL)) {
    flags |= kEventFlagControlDown;
  }
  if (IsWindowsKeyDown(VK_MENU)) {
    flags |= kEventFlagAltDown;
  }

  // Low bit set from GetKeyState indicates "toggled".
  if (::GetKeyState(VK_NUMLOCK) & 1) {
    flags |= kEventFlagNumLockOn;
  }
  if (::GetKeyState(VK_CAPITAL) & 1) {
    flags |= kEventFlagCapsLockOn;
  }
  if (static_cast<uintptr_t>(l_param) & kWindowsKeyPreviousDownFlag) {
    flags |= kEventFlagIsRepeat;
  }

  const int windows_key_code = static_cast<int>(w_param);
  const bool is_extended =
      ((static_cast<uintptr_t>(l_param) >> 16) & KF_EXTENDED) != 0;
  switch (windows_key_code) {
    case VK_RETURN:
      if (is_extended) {
        flags |= kEventFlagIsKeyPad;
      }
      break;
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
      if (!is_extended) {
        flags |= kEventFlagIsKeyPad;
      }
      break;
    case VK_NUMLOCK:
    case VK_NUMPAD0:
    case VK_NUMPAD1:
    case VK_NUMPAD2:
    case VK_NUMPAD3:
    case VK_NUMPAD4:
    case VK_NUMPAD5:
    case VK_NUMPAD6:
    case VK_NUMPAD7:
    case VK_NUMPAD8:
    case VK_NUMPAD9:
    case VK_DIVIDE:
    case VK_MULTIPLY:
    case VK_SUBTRACT:
    case VK_ADD:
    case VK_DECIMAL:
    case VK_CLEAR:
      flags |= kEventFlagIsKeyPad;
      break;
    case VK_SHIFT:
      if (IsWindowsKeyDown(VK_LSHIFT)) {
        flags |= kEventFlagIsLeft;
      } else if (IsWindowsKeyDown(VK_RSHIFT)) {
        flags |= kEventFlagIsRight;
      }
      break;
    case VK_CONTROL:
      if (IsWindowsKeyDown(VK_LCONTROL)) {
        flags |= kEventFlagIsLeft;
      } else if (IsWindowsKeyDown(VK_RCONTROL)) {
        flags |= kEventFlagIsRight;
      }
      break;
    case VK_MENU:
      if (IsWindowsKeyDown(VK_LMENU)) {
        flags |= kEventFlagIsLeft;
      } else if (IsWindowsKeyDown(VK_RMENU)) {
        flags |= kEventFlagIsRight;
      }
      break;
    case VK_LWIN:
      flags |= kEventFlagIsLeft;
      break;
    case VK_RWIN:
      flags |= kEventFlagIsRight;
      break;
    default:
      break;
  }
  return flags;
}

bool IsExtendedWindowsVirtualKey(int windows_key_code) {
  switch (windows_key_code) {
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_DIVIDE:
    case VK_NUMLOCK:
    case VK_RCONTROL:
    case VK_RMENU:
      return true;
    default:
      return false;
  }
}

uint32_t BuildWindowsNativeKeyCode(int windows_key_code,
                                   int native_scan_code,
                                   bool was_key_down,
                                   bool is_key_up) {
  int scan_code = native_scan_code & 0xff;
  if (scan_code == 0 && windows_key_code != 0) {
    scan_code = static_cast<int>(
        ::MapVirtualKeyW(static_cast<UINT>(windows_key_code),
                         MAPVK_VK_TO_VSC)) &
                0xff;
  }

  uint32_t native_key_code =
      kWindowsKeyRepeatCount |
      (static_cast<uint32_t>(scan_code) << kWindowsKeyScanCodeShift);
  if (IsExtendedWindowsVirtualKey(windows_key_code)) {
    native_key_code |= kWindowsKeyExtendedFlag;
  }
  if (was_key_down || is_key_up) {
    native_key_code |= kWindowsKeyPreviousDownFlag;
  }
  if (is_key_up) {
    native_key_code |= kWindowsKeyTransitionUpFlag;
  }
  return native_key_code;
}

}  // namespace offscreen
