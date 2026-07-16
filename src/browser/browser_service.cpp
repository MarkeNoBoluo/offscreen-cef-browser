#include "browser/browser_service.h"

#include <atomic>
#include <sstream>
#include <utility>

#include "app/diagnostic_log.h"
#include "browser/browser_frame.h"
#include "include/cef_frame.h"

namespace offscreen {

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

}  // namespace offscreen
