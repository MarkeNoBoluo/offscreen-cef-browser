#pragma once

#include <functional>
#include <string>

#include <windows.h>

#include "browser/browser_close_state.h"
#include "browser/browser_client.h"
#include "browser/browser_geometry.h"
#include "browser/minimal_render_handler.h"
#include "include/cef_browser.h"

namespace offscreen {

class BrowserService final : public BrowserClient::Delegate {
 public:
  using BrowserClosedCallback = std::function<void()>;

  BrowserService();
  ~BrowserService() override;

  bool CreateBrowser(HWND parent_handle,
                     BrowserViewRect initial_view_rect,
                     const std::string& initial_url);
  void SetBrowserClosedCallback(BrowserClosedCallback browser_closed_callback);
  void Resize(BrowserViewRect view_rect);
  void Navigate(const std::string& url);
  void Reload();
  void Stop();
  bool TryCloseBrowser();

  bool has_browser() const;
  bool is_closing() const;
  std::string title() const;
  std::string last_error() const;

  void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBrowserClosing(CefRefPtr<CefBrowser> browser) override;
  void OnBrowserClosed(CefRefPtr<CefBrowser> browser) override;
  void OnLoadStateChanged(bool is_loading,
                          bool can_go_back,
                          bool can_go_forward) override;
  void OnTitleChanged(const std::string& title) override;
  void OnLoadErrorText(const std::string& error_text) override;
  void OnRenderProcessTerminated() override;

 private:
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<MinimalRenderHandler> render_handler_;
  CefRefPtr<BrowserClient> client_;
  bool close_when_created_ = false;
  bool is_closing_ = false;
  bool is_loading_ = false;
  bool can_go_back_ = false;
  bool can_go_forward_ = false;
  BrowserClosedCallback browser_closed_callback_;
  std::string title_;
  std::string last_error_;
};

}  // namespace offscreen
