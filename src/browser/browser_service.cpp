#include "browser/browser_service.h"

#include <atomic>
#include <sstream>
#include <utility>

#include "app/diagnostic_log.h"
#include "browser/browser_frame.h"
#include "browser/browser_input_mapping.h"
#include "include/cef_frame.h"

namespace offscreen {

namespace {

bool IsWindowsSystemKeyMessage(uint32_t message) {
  return message == WM_SYSCHAR || message == WM_SYSKEYDOWN ||
         message == WM_SYSKEYUP;
}

bool IsWindowsKeyDownMessage(uint32_t message) {
  return message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
}

bool IsWindowsKeyUpMessage(uint32_t message) {
  return message == WM_KEYUP || message == WM_SYSKEYUP;
}

bool IsWindowsCharMessage(uint32_t message) {
  return message == WM_CHAR || message == WM_SYSCHAR;
}

}  // namespace

BrowserService::BrowserService()
    : frame_(std::make_shared<BrowserFrame>()) {
  DiagnosticLog("BrowserService constructed frame=" +
                HexValue(reinterpret_cast<uintptr_t>(frame_.get())));
}

BrowserService::~BrowserService() = default;

void BrowserService::SetBrowserClosedCallback(
    BrowserClosedCallback browser_closed_callback) {
  DiagnosticLog("BrowserService::SetBrowserClosedCallback");
  browser_closed_callback_ = std::move(browser_closed_callback);
}

void BrowserService::SetPaintUpdateCallback(PaintUpdateCallback callback) {
  DiagnosticLog("BrowserService::SetPaintUpdateCallback");
  paint_update_callback_ = std::move(callback);
}

void BrowserService::SetCursorChangeCallback(CursorChangeCallback callback) {
  DiagnosticLog("BrowserService::SetCursorChangeCallback");
  cursor_change_callback_ = std::move(callback);
}

bool BrowserService::CreateBrowser(HWND parent_handle,
                                   BrowserViewRect initial_view_rect,
                                   double initial_device_scale_factor,
                                   const std::string& initial_url) {
  if (browser_ || client_) {
    DiagnosticLog("BrowserService::CreateBrowser rejected: browser/client "
                  "already exists");
    return false;
  }

  if (!frame_) {
    frame_ = std::make_shared<BrowserFrame>();
  }
  render_handler_ = new OsrRenderHandler(initial_view_rect,
                                          initial_device_scale_factor, frame_,
                                          paint_update_callback_);
  client_ = new BrowserClient(this, render_handler_);

  {
    std::ostringstream stream;
    stream << "BrowserService::CreateBrowser parent="
           << HexValue(reinterpret_cast<uintptr_t>(parent_handle)) << " rect="
           << initial_view_rect.x << "," << initial_view_rect.y << " "
           << initial_view_rect.width << "x" << initial_view_rect.height
           << " scale=" << initial_device_scale_factor << " url=["
           << initial_url << "] frame="
           << HexValue(reinterpret_cast<uintptr_t>(frame_.get()));
    DiagnosticLog(stream.str());
  }

  CefWindowInfo window_info;
  window_info.SetAsWindowless(parent_handle);

  CefBrowserSettings browser_settings;
  browser_settings.windowless_frame_rate = 30;
  const bool created = CefBrowserHost::CreateBrowser(
      window_info, client_, initial_url, browser_settings, nullptr, nullptr);
  DiagnosticLog(std::string("CefBrowserHost::CreateBrowser returned ") +
                (created ? "true" : "false"));
  if (!created) {
    client_ = nullptr;
    render_handler_ = nullptr;
    last_error_ = "CefBrowserHost::CreateBrowser returned false";
  }
  return created;
}

void BrowserService::Resize(BrowserViewRect view_rect,
                             double device_scale_factor) {
  static std::atomic<int> resize_count{0};
  if (ShouldDiagnosticLog(resize_count, 20, 50)) {
    std::ostringstream stream;
    stream << "BrowserService::Resize rect=" << view_rect.x << ","
           << view_rect.y << " " << view_rect.width << "x" << view_rect.height
           << " scale=" << device_scale_factor
           << " has_render_handler=" << (render_handler_ != nullptr)
           << " has_browser=" << (browser_ != nullptr);
    DiagnosticLog(stream.str());
  }
  if (render_handler_) {
    render_handler_->SetViewRect(view_rect, device_scale_factor);
  }
  if (browser_) {
    browser_->GetHost()->WasResized();
  }
}

void BrowserService::Navigate(const std::string& url) {
  DiagnosticLog("BrowserService::Navigate url=[" + url +
                "] has_browser=" + (browser_ ? "true" : "false"));
  if (browser_ && browser_->GetMainFrame()) {
    browser_->GetMainFrame()->LoadURL(url);
  }
}

void BrowserService::Reload() {
  DiagnosticLog("BrowserService::Reload has_browser=" +
                std::string(browser_ ? "true" : "false"));
  if (browser_) {
    browser_->Reload();
  }
}

void BrowserService::Stop() {
  DiagnosticLog("BrowserService::Stop has_browser=" +
                std::string(browser_ ? "true" : "false"));
  if (browser_) {
    browser_->StopLoad();
  }
}

bool BrowserService::TryCloseBrowser() {
  DiagnosticLog("BrowserService::TryCloseBrowser has_browser=" +
                std::string(browser_ ? "true" : "false") +
                " has_client=" + (client_ ? "true" : "false") +
                " is_closing=" + (is_closing_ ? "true" : "false"));
  switch (DecideBrowserCloseAction(browser_ != nullptr, client_ != nullptr,
                                   is_closing_)) {
    case BrowserCloseAction::AllowQtClose:
      return true;
    case BrowserCloseAction::WaitForBrowserCreation:
      close_when_created_ = true;
      is_closing_ = true;
      return false;
    case BrowserCloseAction::StartBrowserClose:
      is_closing_ = true;
      browser_->GetHost()->TryCloseBrowser();
      return false;
    case BrowserCloseAction::WaitForBrowserClose:
      return false;
  }
  return false;
}

bool BrowserService::has_browser() const {
  return browser_ != nullptr;
}

bool BrowserService::is_closing() const {
  return is_closing_;
}

std::string BrowserService::title() const {
  return title_;
}

std::string BrowserService::last_error() const {
  return last_error_;
}

std::shared_ptr<BrowserFrame> BrowserService::frame() const {
  return frame_;
}

void BrowserService::SendRawKeyDown(int windows_key_code,
                                    uint32_t native_key_code,
                                    int qt_modifiers,
                                    bool is_keypad) {
  if (!browser_) return;

  CefKeyEvent event{};
  event.type = KEYEVENT_RAWKEYDOWN;
  event.windows_key_code = windows_key_code;
  event.native_key_code = static_cast<int>(native_key_code);
  event.modifiers = MapQtModifiersToCefEventFlags(qt_modifiers);
  if (is_keypad) {
    event.modifiers |= kEventFlagIsKeyPad;
  }

  browser_->GetHost()->SendKeyEvent(event);
}

void BrowserService::SendCharEvent(uint32_t native_key_code,
                                   int qt_modifiers,
                                   char16_t character) {
  if (!browser_) return;

  CefKeyEvent event{};
  event.type = KEYEVENT_CHAR;
  event.windows_key_code = static_cast<int>(character);
  event.native_key_code = static_cast<int>(native_key_code);
  event.modifiers = MapQtModifiersToCefEventFlags(qt_modifiers);
  event.character = character;
  event.unmodified_character = character;

  browser_->GetHost()->SendKeyEvent(event);
}

void BrowserService::SendKeyUp(int windows_key_code,
                               uint32_t native_key_code,
                               int qt_modifiers,
                               bool is_keypad) {
  if (!browser_) return;

  CefKeyEvent event{};
  event.type = KEYEVENT_KEYUP;
  event.windows_key_code = windows_key_code;
  event.native_key_code = static_cast<int>(native_key_code);
  event.modifiers = MapQtModifiersToCefEventFlags(qt_modifiers);
  if (is_keypad) {
    event.modifiers |= kEventFlagIsKeyPad;
  }

  browser_->GetHost()->SendKeyEvent(event);
}

void BrowserService::SendWindowsKeyEvent(uint32_t message, uintptr_t w_param,
                                         intptr_t l_param) {
  if (!browser_) return;

  CefKeyEvent event{};
  if (IsWindowsKeyDownMessage(message)) {
    event.type = KEYEVENT_RAWKEYDOWN;
  } else if (IsWindowsKeyUpMessage(message)) {
    event.type = KEYEVENT_KEYUP;
  } else if (IsWindowsCharMessage(message)) {
    event.type = KEYEVENT_CHAR;
  } else {
    return;
  }

  event.windows_key_code = static_cast<int>(w_param);
  event.native_key_code = static_cast<int>(l_param);
  event.is_system_key = IsWindowsSystemKeyMessage(message);
  event.modifiers = MapWindowsKeyboardMessageToCefEventFlags(w_param, l_param);
  if (event.type == KEYEVENT_CHAR) {
    event.character = static_cast<char16_t>(w_param);
    event.unmodified_character = event.character;

    if (w_param <= 0xffff && IsWindowsKeyDown(VK_RMENU)) {
      const SHORT scan_result = ::VkKeyScanExW(
          static_cast<WCHAR>(w_param), ::GetKeyboardLayout(0));
      constexpr int kCtrlAlt = 2 | 4;
      if (scan_result != -1 && ((scan_result >> 8) & kCtrlAlt) == kCtrlAlt) {
        event.modifiers &= ~(kEventFlagControlDown | kEventFlagAltDown);
        event.modifiers |= kEventFlagAltGrDown;
      }
    }
  }

  static std::atomic<int> key_event_count{0};
  if (ShouldDiagnosticLog(key_event_count, 80, 200)) {
    std::ostringstream stream;
    stream << "BrowserService::SendWindowsKeyEvent message=0x" << std::hex
           << message << std::dec << " type=" << event.type
           << " vk=" << event.windows_key_code
           << " native=" << event.native_key_code
           << " modifiers=" << event.modifiers
           << " system=" << event.is_system_key;
    DiagnosticLog(stream.str());
  }

  browser_->GetHost()->SendKeyEvent(event);
}

void BrowserService::SendMouseClickEvent(int x, int y,
                                         int qt_button, int qt_buttons,
                                         bool mouse_up, int click_count,
                                         int qt_modifiers) {
  if (!browser_) return;

  CefMouseEvent mouse_event;
  mouse_event.x = x;
  mouse_event.y = y;
  mouse_event.modifiers = MapQtModifiersToCefEventFlags(qt_modifiers) |
                          MouseButtonsToCefEventFlags(qt_buttons);

  browser_->GetHost()->SendMouseClickEvent(
      mouse_event,
      static_cast<CefBrowserHost::MouseButtonType>(
          MapQtMouseButtonToCefMouseButton(qt_button)),
      mouse_up, click_count);
}

void BrowserService::SendMouseMoveEvent(int x, int y,
                                         int qt_buttons,
                                         int qt_modifiers,
                                         bool mouse_leave) {
  if (!browser_) return;

  CefMouseEvent mouse_event;
  mouse_event.x = x;
  mouse_event.y = y;
  mouse_event.modifiers = MapQtModifiersToCefEventFlags(qt_modifiers) |
                          MouseButtonsToCefEventFlags(qt_buttons);

  browser_->GetHost()->SendMouseMoveEvent(mouse_event, mouse_leave);
}

void BrowserService::SendMouseWheelEvent(int x, int y,
                                          int qt_buttons,
                                          int qt_modifiers,
                                          int delta_x, int delta_y) {
  if (!browser_) return;

  CefMouseEvent mouse_event;
  mouse_event.x = x;
  mouse_event.y = y;
  mouse_event.modifiers = MapQtModifiersToCefEventFlags(qt_modifiers) |
                          MouseButtonsToCefEventFlags(qt_buttons);

  browser_->GetHost()->SendMouseWheelEvent(mouse_event, delta_x, delta_y);
}

void BrowserService::SetBrowserFocus(bool focus) {
  if (!browser_) return;
  browser_->GetHost()->SetFocus(focus);
}

void BrowserService::SendCaptureLost() {
  if (!browser_) return;
  browser_->GetHost()->SendCaptureLostEvent();
}

void BrowserService::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
  DiagnosticLog("BrowserService::OnBrowserCreated browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1));
  browser_ = browser;
  last_error_.clear();
  if (close_when_created_) {
    close_when_created_ = false;
    browser_->GetHost()->TryCloseBrowser();
    return;
  }

  if (browser_) {
    DiagnosticLog("BrowserService::OnBrowserCreated priming OSR: "
                  "WasHidden(false), WasResized(), Invalidate(PET_VIEW)");
    browser_->GetHost()->WasHidden(false);
    browser_->GetHost()->WasResized();
    browser_->GetHost()->Invalidate(PET_VIEW);
  }
}

void BrowserService::OnBrowserClosing(CefRefPtr<CefBrowser> browser) {
  DiagnosticLog("BrowserService::OnBrowserClosing browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1));
  is_closing_ = true;
}

void BrowserService::OnBrowserClosed(CefRefPtr<CefBrowser> browser) {
  DiagnosticLog("BrowserService::OnBrowserClosed browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1));
  if (!browser_ || !browser_->IsSame(browser)) {
    DiagnosticLog("BrowserService::OnBrowserClosed ignored non-current browser");
    return;
  }
  browser_ = nullptr;
  client_ = nullptr;
  render_handler_ = nullptr;
  close_when_created_ = false;
  is_closing_ = false;
  if (browser_closed_callback_) {
    browser_closed_callback_();
  }
}

void BrowserService::OnLoadStateChanged(bool is_loading,
                                        bool can_go_back,
                                        bool can_go_forward) {
  DiagnosticLog("BrowserService::OnLoadStateChanged is_loading=" +
                std::string(is_loading ? "true" : "false") +
                " can_go_back=" + (can_go_back ? "true" : "false") +
                " can_go_forward=" + (can_go_forward ? "true" : "false"));
  is_loading_ = is_loading;
  can_go_back_ = can_go_back;
  can_go_forward_ = can_go_forward;
}

void BrowserService::OnTitleChanged(const std::string& title) {
  DiagnosticLog("BrowserService::OnTitleChanged title=[" + title + "]");
  title_ = title;
}

void BrowserService::OnLoadErrorText(const std::string& error_text) {
  DiagnosticLog("BrowserService::OnLoadErrorText error=[" + error_text + "]");
  last_error_ = error_text;
}

void BrowserService::OnRenderProcessTerminated() {
  DiagnosticLog("BrowserService::OnRenderProcessTerminated");
  last_error_ = "CEF render process terminated";
}

void BrowserService::OnCursorChanged(int cursor_type,
                                     CefCursorHandle cursor_handle) {
  DiagnosticLog("BrowserService::OnCursorChanged type=" +
                std::to_string(cursor_type));
  if (cursor_change_callback_) {
    cursor_change_callback_(cursor_type, cursor_handle);
  }
}

void BrowserService::OnTakeFocusRequest(bool next) {
  DiagnosticLog("BrowserService::OnTakeFocusRequest next=" +
                std::string(next ? "true" : "false"));
}

void BrowserService::OnSetFocusRequest() {
  DiagnosticLog("BrowserService::OnSetFocusRequest");
}

}  // namespace offscreen
