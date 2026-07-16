#include "browser/browser_client.h"

#include "include/cef_frame.h"

namespace offscreen {

namespace {

std::string CefStringToUtf8(const CefString& value) {
  return value.ToString();
}

}  // namespace

BrowserClient::BrowserClient(Delegate* delegate,
                             CefRefPtr<MinimalRenderHandler> render_handler)
    : delegate_(delegate), render_handler_(render_handler) {}

CefRefPtr<CefLifeSpanHandler> BrowserClient::GetLifeSpanHandler() {
  return this;
}

CefRefPtr<CefLoadHandler> BrowserClient::GetLoadHandler() {
  return this;
}

CefRefPtr<CefDisplayHandler> BrowserClient::GetDisplayHandler() {
  return this;
}

CefRefPtr<CefRenderHandler> BrowserClient::GetRenderHandler() {
  return render_handler_;
}

CefRefPtr<CefRequestHandler> BrowserClient::GetRequestHandler() {
  return this;
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  if (delegate_) {
    delegate_->OnBrowserCreated(browser);
  }
}

bool BrowserClient::DoClose(CefRefPtr<CefBrowser> browser) {
  if (delegate_) {
    delegate_->OnBrowserClosing(browser);
  }
  return false;
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  if (delegate_) {
    delegate_->OnBrowserClosed(browser);
  }
}

void BrowserClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                         bool isLoading,
                                         bool canGoBack,
                                         bool canGoForward) {
  if (delegate_) {
    delegate_->OnLoadStateChanged(isLoading, canGoBack, canGoForward);
  }
}

void BrowserClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                ErrorCode errorCode,
                                const CefString& errorText,
                                const CefString& failedUrl) {
  if (!frame || !frame->IsMain()) {
    return;
  }
  if (delegate_) {
    delegate_->OnLoadErrorText(CefStringToUtf8(errorText));
  }
}

void BrowserClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                  const CefString& title) {
  if (delegate_) {
    delegate_->OnTitleChanged(CefStringToUtf8(title));
  }
}

void BrowserClient::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                              TerminationStatus status) {
  if (delegate_) {
    delegate_->OnRenderProcessTerminated();
  }
}

}  // namespace offscreen
