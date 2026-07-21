#pragma once

#include <functional>
#include <cstdint>
#include <memory>
#include <string>

#include <windows.h>

#include "browser/browser_client.h"
#include "browser/browser_close_state.h"
#include "browser/browser_geometry.h"
#include "browser/browser_paint_geometry.h"
#include "browser/osr_render_handler.h"
#include "include/cef_browser.h"

namespace offscreen {

class BrowserFrame;

class BrowserService final : public BrowserClient::Delegate {
 public:
  using BrowserClosedCallback = std::function<void()>;
  using CursorChangeCallback =
      std::function<void(int cursor_type, CefCursorHandle cursor_handle)>;
  using ImeCompositionRangeChangedCallback =
      std::function<void(const CefRange&, const std::vector<CefRect>&)>;
  using PopupRequestCallback = std::function<void(const std::string& url)>;
  using AddressChangeCallback = std::function<void(const std::string& url)>;
  using TitleChangeCallback = std::function<void(const std::string& title)>;
  using LoadStateChangeCallback =
      std::function<void(bool is_loading, bool can_go_back, bool can_go_forward)>;
  using LoadErrorCallback = std::function<void(const std::string& error_text)>;

  BrowserService();
  ~BrowserService() override;

  bool CreateBrowser(HWND parent_handle,
                     BrowserViewRect initial_view_rect,
                     double initial_device_scale_factor,
                     const std::string& initial_url);
  void SetBrowserClosedCallback(BrowserClosedCallback browser_closed_callback);
  void SetPaintUpdateCallback(PaintUpdateCallback callback);
  void SetCursorChangeCallback(CursorChangeCallback callback);
  void SetImeCompositionRangeChangedCallback(
      ImeCompositionRangeChangedCallback callback);
  void SetPopupRequestCallback(PopupRequestCallback callback);
  void SetAddressChangeCallback(AddressChangeCallback callback);
  void SetTitleChangeCallback(TitleChangeCallback callback);
  void SetLoadStateChangeCallback(LoadStateChangeCallback callback);
  void SetLoadErrorCallback(LoadErrorCallback callback);
  void Resize(BrowserViewRect view_rect, double device_scale_factor);
  void Navigate(const std::string& url);
  void Reload();
  void Stop();
  bool TryCloseBrowser();

  bool has_browser() const;
  bool is_closing() const;
  std::string title() const;
  std::string address() const;
  std::string last_error() const;
  std::shared_ptr<BrowserFrame> frame() const;

  // Keyboard input (three-method separation matching CEF key event types)
  void SendRawKeyDown(int windows_key_code, uint32_t native_key_code,
                      int qt_modifiers, bool is_keypad);
  void SendCharEvent(uint32_t native_key_code,
                     int qt_modifiers, char16_t character);
  void SendKeyUp(int windows_key_code, uint32_t native_key_code,
                 int qt_modifiers, bool is_keypad);
  void SendWindowsKeyEvent(uint32_t message, uintptr_t w_param,
                           intptr_t l_param);

  // Mouse input
  void SendMouseClickEvent(int x, int y, int qt_button, int qt_buttons,
                           bool mouse_up, int click_count, int qt_modifiers);
  void SendMouseMoveEvent(int x, int y, int qt_buttons,
                           int qt_modifiers, bool mouse_leave);
  void SendMouseWheelEvent(int x, int y, int qt_buttons,
                            int qt_modifiers, int delta_x, int delta_y);

  // Focus
  void SetBrowserFocus(bool focus);
  void SendCaptureLost();

  // IME
  void ImeSetComposition(const CefString& text,
                         const std::vector<CefCompositionUnderline>& underlines,
                         const CefRange& replacement_range,
                         const CefRange& selection_range);
  void ImeCommitText(const CefString& text,
                     const CefRange& replacement_range,
                     int relative_cursor_pos);
  void ImeCancelComposition();
  void ImeFinishComposingText(bool keep_selection);

  // BrowserClient::Delegate overrides
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBrowserClosing(CefRefPtr<CefBrowser> browser) override;
  void OnBrowserClosed(CefRefPtr<CefBrowser> browser) override;
  void OnLoadStateChanged(bool is_loading,
                          bool can_go_back,
                          bool can_go_forward) override;
  void OnAddressChanged(const std::string& url) override;
  void OnTitleChanged(const std::string& title) override;
  void OnLoadErrorText(const std::string& error_text) override;
  void OnRenderProcessTerminated() override;
  void OnCursorChanged(int cursor_type, CefCursorHandle cursor_handle) override;
  void OnTakeFocusRequest(bool next) override;
  void OnSetFocusRequest() override;
  void OnPopupRequest(const std::string& url) override;

 private:
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<OsrRenderHandler> render_handler_;
  CefRefPtr<BrowserClient> client_;
  std::shared_ptr<BrowserFrame> frame_;
  PaintUpdateCallback paint_update_callback_;
  CursorChangeCallback cursor_change_callback_;
  ImeCompositionRangeChangedCallback ime_composition_range_changed_callback_;
  bool close_when_created_ = false;
  bool is_closing_ = false;
  bool is_loading_ = false;
  bool can_go_back_ = false;
  bool can_go_forward_ = false;
  BrowserClosedCallback browser_closed_callback_;
  PopupRequestCallback popup_request_callback_;
  AddressChangeCallback address_change_callback_;
  TitleChangeCallback title_change_callback_;
  LoadStateChangeCallback load_state_change_callback_;
  LoadErrorCallback load_error_callback_;
  std::string address_;
  std::string title_;
  std::string last_error_;
};

}  // namespace offscreen
