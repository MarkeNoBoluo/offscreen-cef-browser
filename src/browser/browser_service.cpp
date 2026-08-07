#include "browser/browser_service.h"

#include <atomic>
#include <sstream>
#include <utility>

#include "app/diagnostic_log.h"
#include "browser/browser_frame.h"
#include "browser/browser_input_mapping.h"
#include "browser/render_stats.h"
#include "include/cef_frame.h"

namespace offscreen {

namespace {

/// 判断消息是否为带 Alt 语义的 Win32 系统键消息。
/// @param message Win32 消息编号。
/// @return 系统键消息时为 true。
bool IsWindowsSystemKeyMessage(uint32_t message) {
  return message == WM_SYSCHAR || message == WM_SYSKEYDOWN ||
         message == WM_SYSKEYUP;
}

/// 判断消息是否表示按键按下。
/// @param message Win32 消息编号。
/// @return 按下消息时为 true。
bool IsWindowsKeyDownMessage(uint32_t message) {
  return message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
}

/// 判断消息是否表示按键抬起。
/// @param message Win32 消息编号。
/// @return 抬起消息时为 true。
bool IsWindowsKeyUpMessage(uint32_t message) {
  return message == WM_KEYUP || message == WM_SYSKEYUP;
}

/// 判断消息是否携带可提交字符。
/// @param message Win32 消息编号。
/// @return 字符消息时为 true。
bool IsWindowsCharMessage(uint32_t message) {
  return message == WM_CHAR || message == WM_SYSCHAR;
}

/// 将 Qt 坐标、按键和修饰键转换为 CEF 鼠标事件。
/// @param x 逻辑 x 坐标。
/// @param y 逻辑 y 坐标。
/// @param qt_buttons 当前全部按下鼠标键。
/// @param qt_modifiers Qt 修饰键位掩码。
/// @return CEF 鼠标事件。
CefMouseEvent MakeCefMouseEvent(int x, int y, int qt_buttons,
                                int qt_modifiers) {
  CefMouseEvent mouse_event;
  mouse_event.x = x;
  mouse_event.y = y;
  mouse_event.modifiers = MapQtModifiersToCefEventFlags(qt_modifiers) |
                          MouseButtonsToCefEventFlags(qt_buttons);
  return mouse_event;
}

}  // namespace

BrowserService::BrowserService()
    : frame_(std::make_shared<BrowserFrame>()),
      render_stats_(std::make_shared<RenderStats>()) {
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

void BrowserService::SetStartDraggingCallback(
    StartDraggingCallback callback) {
  DiagnosticLog("BrowserService::SetStartDraggingCallback");
  start_dragging_callback_ = std::move(callback);
  if (render_handler_) {
    render_handler_->SetStartDraggingCallback(start_dragging_callback_);
  }
}

void BrowserService::SetUpdateDragCursorCallback(
    UpdateDragCursorCallback callback) {
  DiagnosticLog("BrowserService::SetUpdateDragCursorCallback");
  update_drag_cursor_callback_ = std::move(callback);
  if (render_handler_) {
    render_handler_->SetUpdateDragCursorCallback(update_drag_cursor_callback_);
  }
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
                                          paint_update_callback_,
                                          render_stats_);
  if (ime_composition_range_changed_callback_) {
    render_handler_->SetImeCompositionRangeChangedCallback(
        ime_composition_range_changed_callback_);
  }
  if (start_dragging_callback_) {
    render_handler_->SetStartDraggingCallback(start_dragging_callback_);
  }
  if (update_drag_cursor_callback_) {
    render_handler_->SetUpdateDragCursorCallback(update_drag_cursor_callback_);
  }
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

  address_ = initial_url;

  CefWindowInfo window_info;
  window_info.SetAsWindowless(parent_handle);
  // 开启 D3D11 共享纹理路径：CEF 才会调用
  // CefRenderHandler::OnAcceleratedPaint（见 include/cef_render_handler.h，
  // "only called when CefWindowInfo::shared_texture_enabled is set to true"）。
  // 开启后主视图帧不再走 OnPaint（CPU buffer），改由 OnAcceleratedPaint
  // 提供共享句柄；OsrRenderHandler 负责 OpenSharedResource 读回像素。
  window_info.shared_texture_enabled = TRUE;

  CefBrowserSettings browser_settings;
  browser_settings.windowless_frame_rate = 30;
  browser_settings.javascript_access_clipboard = STATE_ENABLED;
  browser_settings.javascript_dom_paste = STATE_ENABLED;
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

void BrowserService::SetImeCompositionRangeChangedCallback(
    ImeCompositionRangeChangedCallback callback) {
  DiagnosticLog("BrowserService::SetImeCompositionRangeChangedCallback");
  ime_composition_range_changed_callback_ = std::move(callback);
  if (render_handler_) {
    render_handler_->SetImeCompositionRangeChangedCallback(
        ime_composition_range_changed_callback_);
  }
}

void BrowserService::SetPopupRequestCallback(PopupRequestCallback callback) {
  DiagnosticLog("BrowserService::SetPopupRequestCallback");
  popup_request_callback_ = std::move(callback);
}

void BrowserService::SetAddressChangeCallback(AddressChangeCallback callback) {
  DiagnosticLog("BrowserService::SetAddressChangeCallback");
  address_change_callback_ = std::move(callback);
}

void BrowserService::SetTitleChangeCallback(TitleChangeCallback callback) {
  DiagnosticLog("BrowserService::SetTitleChangeCallback");
  title_change_callback_ = std::move(callback);
}

void BrowserService::SetLoadStateChangeCallback(
    LoadStateChangeCallback callback) {
  DiagnosticLog("BrowserService::SetLoadStateChangeCallback");
  load_state_change_callback_ = std::move(callback);
}

void BrowserService::SetLoadErrorCallback(LoadErrorCallback callback) {
  DiagnosticLog("BrowserService::SetLoadErrorCallback");
  load_error_callback_ = std::move(callback);
}

void BrowserService::SetDownloadDirectory(const std::wstring& path) {
  DiagnosticLog("BrowserService::SetDownloadDirectory path_empty=" +
                std::string(path.empty() ? "true" : "false"));
  download_dir_ = path;
}

void BrowserService::SetDownloadStateChangeCallback(
    DownloadStateChangeCallback callback) {
  DiagnosticLog("BrowserService::SetDownloadStateChangeCallback");
  download_state_change_callback_ = std::move(callback);
}

void BrowserService::SetContextMenuRequestedCallback(
    ContextMenuRequestedCallback callback) {
  DiagnosticLog("BrowserService::SetContextMenuRequestedCallback");
  context_menu_requested_callback_ = std::move(callback);
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
  address_ = url;
  last_error_.clear();
  if (address_change_callback_) {
    address_change_callback_(address_);
  }
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

void BrowserService::GoBack() {
  DiagnosticLog("BrowserService::GoBack has_browser=" +
                std::string(browser_ ? "true" : "false"));
  if (browser_) {
    browser_->GoBack();
  }
}

void BrowserService::GoForward() {
  DiagnosticLog("BrowserService::GoForward has_browser=" +
                std::string(browser_ ? "true" : "false"));
  if (browser_) {
    browser_->GoForward();
  }
}

void BrowserService::Copy() {
  if (browser_ && browser_->GetMainFrame()) {
    browser_->GetMainFrame()->Copy();
  }
}

void BrowserService::Cut() {
  if (browser_ && browser_->GetMainFrame()) {
    browser_->GetMainFrame()->Cut();
  }
}

void BrowserService::Paste() {
  if (browser_ && browser_->GetMainFrame()) {
    browser_->GetMainFrame()->Paste();
  }
}

void BrowserService::SelectAll() {
  if (browser_ && browser_->GetMainFrame()) {
    browser_->GetMainFrame()->SelectAll();
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

bool BrowserService::can_go_back() const {
  return can_go_back_;
}

bool BrowserService::can_go_forward() const {
  return can_go_forward_;
}

bool BrowserService::is_loading() const {
  return is_loading_;
}

std::string BrowserService::title() const {
  return title_;
}

std::string BrowserService::address() const {
  return address_;
}

std::string BrowserService::last_error() const {
  return last_error_;
}

std::shared_ptr<BrowserFrame> BrowserService::frame() const {
  return frame_;
}

std::shared_ptr<RenderStats> BrowserService::render_stats() const {
  return render_stats_;
}

void BrowserService::SetRenderStatsEnabled(bool enabled) {
  if (enabled) {
    render_stats_->Enable();
  } else {
    render_stats_->Disable();
  }
}

bool BrowserService::render_stats_enabled() const {
  return render_stats_->enabled();
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

  const CefMouseEvent mouse_event =
      MakeCefMouseEvent(x, y, qt_buttons, qt_modifiers);

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

  const CefMouseEvent mouse_event =
      MakeCefMouseEvent(x, y, qt_buttons, qt_modifiers);

  browser_->GetHost()->SendMouseMoveEvent(mouse_event, mouse_leave);
}

void BrowserService::SendMouseWheelEvent(int x, int y,
                                          int qt_buttons,
                                          int qt_modifiers,
                                          int delta_x, int delta_y) {
  if (!browser_) return;

  const CefMouseEvent mouse_event =
      MakeCefMouseEvent(x, y, qt_buttons, qt_modifiers);

  browser_->GetHost()->SendMouseWheelEvent(mouse_event, delta_x, delta_y);
}

void BrowserService::SendDragTargetDragEnter(CefRefPtr<CefDragData> drag_data,
                                             int x, int y, int qt_buttons,
                                             int qt_modifiers,
                                             CefBrowserHost::DragOperationsMask
                                                 allowed_ops) {
  std::ostringstream stream;
  stream << "BrowserService::SendDragTargetDragEnter has_browser="
         << (browser_ ? "true" : "false")
         << " has_drag_data=" << (drag_data ? "true" : "false")
         << " allowed_ops=" << allowed_ops << " pos=" << x << "," << y;
  DiagnosticLog(stream.str());
  if (!browser_ || !drag_data) return;

  CefRefPtr<CefDragData> target_drag_data =
      drag_data->IsReadOnly() ? drag_data->Clone() : drag_data;
  if (!target_drag_data) return;
  target_drag_data->ResetFileContents();
  const CefMouseEvent mouse_event =
      MakeCefMouseEvent(x, y, qt_buttons, qt_modifiers);
  browser_->GetHost()->DragTargetDragEnter(target_drag_data, mouse_event,
                                           allowed_ops);
}

void BrowserService::SendDragTargetDragOver(int x, int y, int qt_buttons,
                                            int qt_modifiers,
                                            CefBrowserHost::DragOperationsMask
                                                allowed_ops) {
  if (!browser_) return;

  const CefMouseEvent mouse_event =
      MakeCefMouseEvent(x, y, qt_buttons, qt_modifiers);
  browser_->GetHost()->DragTargetDragOver(mouse_event, allowed_ops);
}

void BrowserService::SendDragTargetDragLeave() {
  DiagnosticLog("BrowserService::SendDragTargetDragLeave has_browser=" +
                std::string(browser_ ? "true" : "false"));
  if (!browser_) return;
  browser_->GetHost()->DragTargetDragLeave();
}

void BrowserService::SendDragTargetDrop(int x, int y, int qt_buttons,
                                        int qt_modifiers) {
  DiagnosticLog("BrowserService::SendDragTargetDrop has_browser=" +
                std::string(browser_ ? "true" : "false"));
  if (!browser_) return;

  const CefMouseEvent mouse_event =
      MakeCefMouseEvent(x, y, qt_buttons, qt_modifiers);
  browser_->GetHost()->DragTargetDrop(mouse_event);
}

void BrowserService::SendDragSourceEndedAt(
    int x,
    int y,
    CefBrowserHost::DragOperationsMask op) {
  DiagnosticLog("BrowserService::SendDragSourceEndedAt has_browser=" +
                std::string(browser_ ? "true" : "false") +
                " op=" + std::to_string(op) + " pos=" + std::to_string(x) +
                "," + std::to_string(y));
  if (!browser_) return;
  browser_->GetHost()->DragSourceEndedAt(x, y, op);
}

void BrowserService::SendDragSourceSystemDragEnded() {
  DiagnosticLog("BrowserService::SendDragSourceSystemDragEnded has_browser=" +
                std::string(browser_ ? "true" : "false"));
  if (!browser_) return;
  browser_->GetHost()->DragSourceSystemDragEnded();
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
  if (load_state_change_callback_) {
    load_state_change_callback_(is_loading, can_go_back, can_go_forward);
  }
}

void BrowserService::OnAddressChanged(const std::string& url) {
  DiagnosticLog("BrowserService::OnAddressChanged url=[" + url + "]");
  address_ = url;
  if (address_change_callback_) {
    address_change_callback_(url);
  }
}

void BrowserService::OnTitleChanged(const std::string& title) {
  DiagnosticLog("BrowserService::OnTitleChanged title=[" + title + "]");
  title_ = title;
  if (title_change_callback_) {
    title_change_callback_(title);
  }
}

void BrowserService::OnLoadError(int error_code,
                                 const std::string& failed_url,
                                 const std::string& error_text) {
  DiagnosticLog("BrowserService::OnLoadError code=" +
                std::to_string(error_code) + " url=[" + failed_url +
                "] error=[" + error_text + "]");
  last_error_ = error_text;
  if (load_error_callback_) {
    load_error_callback_(error_code, failed_url, error_text);
  }
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

void BrowserService::OnPopupRequest(const std::string& url) {
  DiagnosticLog("BrowserService::OnPopupRequest url=[" + url + "]");
  if (popup_request_callback_) {
    popup_request_callback_(url);
  }
}

void BrowserService::OnDownloadStarted(
    CefRefPtr<CefBeforeDownloadCallback> callback,
    const std::string& suggested_name) {
  DiagnosticLog("BrowserService::OnDownloadStarted suggested_name=[" +
                suggested_name + "] has_dir=" +
                std::string(download_dir_.empty() ? "false" : "true"));
  std::wstring download_path;
  if (!download_dir_.empty()) {
    download_path =
        download_dir_ + L"\\" + CefString(suggested_name).ToWString();
  }
  if (callback) {
    callback->Continue(download_path, false);
  }
  OnDownloadStateChanged(0, suggested_name, CefString(download_path).ToString());
}

void BrowserService::OnDownloadStateChanged(int state,
                                            const std::string& file_name,
                                            const std::string& full_path) {
  DiagnosticLog("BrowserService::OnDownloadStateChanged state=" +
                std::to_string(state) + " name=[" + file_name + "] path=[" +
                full_path + "]");
  if (download_state_change_callback_) {
    download_state_change_callback_(state, file_name, full_path);
  }
}

void BrowserService::OnContextMenuRequested(int view_x, int view_y) {
  DiagnosticLog("BrowserService::OnContextMenuRequested x=" +
                std::to_string(view_x) + " y=" + std::to_string(view_y));
  if (context_menu_requested_callback_) {
    context_menu_requested_callback_(view_x, view_y);
  }
}

void BrowserService::ImeSetComposition(
    const CefString& text,
    const std::vector<CefCompositionUnderline>& underlines,
    const CefRange& replacement_range,
    const CefRange& selection_range) {
  DiagnosticLog("BrowserService::ImeSetComposition has_browser=" +
                std::string(browser_ ? "true" : "false") +
                " text_len=" + std::to_string(text.length()) +
                " underlines=" + std::to_string(underlines.size()) +
                " replacement=" + std::to_string(replacement_range.from) +
                "-" + std::to_string(replacement_range.to) +
                " selection=" + std::to_string(selection_range.from) + "-" +
                std::to_string(selection_range.to));
  if (!browser_) return;
  browser_->GetHost()->ImeSetComposition(text, underlines,
                                         replacement_range, selection_range);
}

void BrowserService::ImeCommitText(const CefString& text,
                                   const CefRange& replacement_range,
                                   int relative_cursor_pos) {
  DiagnosticLog("BrowserService::ImeCommitText has_browser=" +
                std::string(browser_ ? "true" : "false") +
                " text_len=" + std::to_string(text.length()) +
                " replacement=" + std::to_string(replacement_range.from) +
                "-" + std::to_string(replacement_range.to) +
                " cursor=" + std::to_string(relative_cursor_pos));
  if (!browser_) return;
  browser_->GetHost()->ImeCommitText(text, replacement_range,
                                     relative_cursor_pos);
}

void BrowserService::ImeCancelComposition() {
  DiagnosticLog("BrowserService::ImeCancelComposition has_browser=" +
                std::string(browser_ ? "true" : "false"));
  if (!browser_) return;
  browser_->GetHost()->ImeCancelComposition();
}

void BrowserService::ImeFinishComposingText(bool keep_selection) {
  DiagnosticLog("BrowserService::ImeFinishComposingText has_browser=" +
                std::string(browser_ ? "true" : "false") +
                " keep_selection=" +
                (keep_selection ? std::string("true") : std::string("false")));
  if (!browser_) return;
  browser_->GetHost()->ImeFinishComposingText(keep_selection);
}

}  // namespace offscreen
