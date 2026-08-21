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

/// 将 Qt 修饰键位掩码转换为 CEF 事件标志。
/// @param qt_modifiers Qt::KeyboardModifiers 的整数值。
/// @return CEF 修饰键标志。
uint32_t MapQtModifiersToCefEventFlags(int qt_modifiers);
/// 将 Qt 鼠标按键转换为 CEF 鼠标按键类型。
/// @param qt_mouse_button Qt::MouseButton 的整数值。
/// @return CEF 鼠标按键常量。
int MapQtMouseButtonToCefMouseButton(int qt_mouse_button);
/// 将当前按下的 Qt 鼠标按钮转换为 CEF 修饰标志。
/// @param qt_mouse_buttons Qt::MouseButtons 的整数值。
/// @return 包含鼠标按钮的 CEF 标志。
uint32_t MouseButtonsToCefEventFlags(int qt_mouse_buttons);
/// 判断 Qt 鼠标事件是否应作为真实鼠标输入转发给 CEF。
/// @param qt_mouse_event_source Qt::MouseEventSource 的整数值。
/// @return 非合成鼠标事件返回 true。
bool ShouldForwardQtMouseEvent(int qt_mouse_event_source);
/// 从 Win32 键盘消息构造 CEF 修饰键标志。
/// @param w_param Win32 键盘消息的 wParam。
/// @param l_param Win32 键盘消息的 lParam。
/// @return CEF 修饰键标志。
uint32_t MapWindowsKeyboardMessageToCefEventFlags(uintptr_t w_param,
                                                  intptr_t l_param);
/// 查询指定虚拟键是否处于按下状态。
/// @param windows_key_code Win32 虚拟键码。
/// @return 键按下时为 true。
bool IsWindowsKeyDown(int windows_key_code);
/// 判断虚拟键是否必须在原生键码中携带扩展位。
/// @param windows_key_code Win32 虚拟键码。
/// @return 扩展键时为 true。
bool IsExtendedWindowsVirtualKey(int windows_key_code);
/// 根据虚拟键和扫描码拼装 CEF 需要的 Win32 原生键码。
/// @param windows_key_code Win32 虚拟键码。
/// @param native_scan_code 键盘扫描码。
/// @param was_key_down 消息前该键是否已经按下。
/// @param is_key_up 当前消息是否为抬起事件。
/// @return 可用于 CefKeyEvent::native_key_code 的值。
uint32_t BuildWindowsNativeKeyCode(int windows_key_code,
                                   int native_scan_code,
                                   bool was_key_down,
                                   bool is_key_up);

}  // namespace offscreen
