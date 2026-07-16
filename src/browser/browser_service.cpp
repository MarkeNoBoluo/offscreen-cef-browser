#include "browser/browser_service.h"

#include <utility>

#include "include/cef_frame.h"

namespace offscreen {

BrowserService::BrowserService() = default;

BrowserService::~BrowserService() = default;

void BrowserService::SetBrowserClosedCallback(
    BrowserClosedCallback browser_closed_callback) {
  browser_closed_callback_ = std::move(browser_closed_callback);
}

bool BrowserService::CreateBrowser(HWND parent_handle,
                                   BrowserViewRect initial_view_rect,
                                   const std::string& initial_url) {
  if (browser_ || client_) {
    return false;
  }

  render_handler_ = new MinimalRenderHandler(initial_view_rect);
  client_ = new BrowserClient(this, render_handler_);

  CefWindowInfo window_info;
  window_info.SetAsWindowless(parent_handle);

  CefBrowserSettings browser_settings;
  const bool created = CefBrowserHost::CreateBrowser(
      window_info, client_, initial_url, browser_settings, nullptr, nullptr);
  if (!created) {
    client_ = nullptr;
    render_handler_ = nullptr;
    last_error_ = "CefBrowserHost::CreateBrowser returned false";
  }
  return created;
}

void BrowserService::Resize(BrowserViewRect view_rect) {
  if (render_handler_) {
    render_handler_->SetViewRect(view_rect);
  }
  if (browser_) {
    browser_->GetHost()->WasResized();
  }
}

void BrowserService::Navigate(const std::string& url) {
  if (browser_ && browser_->GetMainFrame()) {
    browser_->GetMainFrame()->LoadURL(url);
  }
}

void BrowserService::Reload() {
  if (browser_) {
    browser_->Reload();
  }
}

void BrowserService::Stop() {
  if (browser_) {
    browser_->StopLoad();
  }
}

bool BrowserService::TryCloseBrowser() {
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

void BrowserService::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  last_error_.clear();
  if (close_when_created_) {
    close_when_created_ = false;
    browser_->GetHost()->TryCloseBrowser();
  }
}

void BrowserService::OnBrowserClosing(CefRefPtr<CefBrowser> browser) {
  is_closing_ = true;
}

void BrowserService::OnBrowserClosed(CefRefPtr<CefBrowser> browser) {
  if (!browser_ || !browser_->IsSame(browser)) {
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
  is_loading_ = is_loading;
  can_go_back_ = can_go_back;
  can_go_forward_ = can_go_forward;
}

void BrowserService::OnTitleChanged(const std::string& title) {
  title_ = title;
}

void BrowserService::OnLoadErrorText(const std::string& error_text) {
  last_error_ = error_text;
}

void BrowserService::OnRenderProcessTerminated() {
  last_error_ = "CEF render process terminated";
}

}  // namespace offscreen
