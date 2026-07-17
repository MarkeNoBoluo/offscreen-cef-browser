#include "browser/browser_client.h"

#include <atomic>
#include <sstream>

#include "app/diagnostic_log.h"
#include "include/cef_frame.h"

namespace offscreen {

namespace {

std::string CefStringToUtf8(const CefString& value) {
  return value.ToString();
}

}  // namespace

BrowserClient::BrowserClient(Delegate* delegate,
                             CefRefPtr<CefRenderHandler> render_handler)
    : delegate_(delegate), render_handler_(render_handler) {
  DiagnosticLog("BrowserClient constructed delegate=" +
                HexValue(reinterpret_cast<uintptr_t>(delegate_)) +
                " render_handler=" +
                HexValue(reinterpret_cast<uintptr_t>(render_handler_.get())));
}

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

CefRefPtr<CefKeyboardHandler> BrowserClient::GetKeyboardHandler() {
  return this;
}

CefRefPtr<CefFocusHandler> BrowserClient::GetFocusHandler() {
  return this;
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  DiagnosticLog("BrowserClient::OnAfterCreated browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1));
  if (delegate_) {
    delegate_->OnBrowserCreated(browser);
  }
}

bool BrowserClient::DoClose(CefRefPtr<CefBrowser> browser) {
  DiagnosticLog("BrowserClient::DoClose browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1));
  if (delegate_) {
    delegate_->OnBrowserClosing(browser);
  }
  return false;
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  DiagnosticLog("BrowserClient::OnBeforeClose browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1));
  if (delegate_) {
    delegate_->OnBrowserClosed(browser);
  }
}

void BrowserClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                         bool isLoading,
                                         bool canGoBack,
                                         bool canGoForward) {
  DiagnosticLog("BrowserClient::OnLoadingStateChange browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1) +
                " isLoading=" + (isLoading ? "true" : "false") +
                " canGoBack=" + (canGoBack ? "true" : "false") +
                " canGoForward=" + (canGoForward ? "true" : "false"));
  if (delegate_) {
    delegate_->OnLoadStateChanged(isLoading, canGoBack, canGoForward);
  }
}

void BrowserClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                ErrorCode errorCode,
                                const CefString& errorText,
                                const CefString& failedUrl) {
  DiagnosticLog("BrowserClient::OnLoadError browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1) +
                " frame_is_main=" +
                ((frame && frame->IsMain()) ? "true" : "false") +
                " code=" + std::to_string(static_cast<int>(errorCode)) +
                " error=[" + CefStringToUtf8(errorText) + "] failed_url=[" +
                CefStringToUtf8(failedUrl) + "]");
  if (!frame || !frame->IsMain()) {
    return;
  }
  if (delegate_) {
    delegate_->OnLoadErrorText(CefStringToUtf8(errorText));
  }
}

void BrowserClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                  const CefString& title) {
  DiagnosticLog("BrowserClient::OnTitleChange browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1) +
                " title=[" + CefStringToUtf8(title) + "]");
  if (delegate_) {
    delegate_->OnTitleChanged(CefStringToUtf8(title));
  }
}

void BrowserClient::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                              TerminationStatus status) {
  DiagnosticLog("BrowserClient::OnRenderProcessTerminated browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1) +
                " status=" + std::to_string(static_cast<int>(status)));
  if (delegate_) {
    delegate_->OnRenderProcessTerminated();
  }
}

bool BrowserClient::OnCursorChange(CefRefPtr<CefBrowser> browser,
                                   CefCursorHandle cursor,
                                   cef_cursor_type_t type,
                                   const CefCursorInfo& custom_cursor_info) {
  DiagnosticLog("BrowserClient::OnCursorChange browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1) +
                " type=" + std::to_string(static_cast<int>(type)));
  if (delegate_) {
    delegate_->OnCursorChanged(static_cast<int>(type), cursor);
  }
  return true;
}

bool BrowserClient::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                                  const CefKeyEvent& event,
                                  CefEventHandle os_event,
                                  bool* is_keyboard_shortcut) {
  if (is_keyboard_shortcut) {
    *is_keyboard_shortcut = false;
  }
  static std::atomic<int> pre_key_event_count{0};
  if (ShouldDiagnosticLog(pre_key_event_count, 80, 200)) {
    std::ostringstream stream;
    stream << "BrowserClient::OnPreKeyEvent browser_id="
           << (browser ? browser->GetIdentifier() : -1)
           << " type=" << event.type
           << " vk=" << event.windows_key_code
           << " native=" << event.native_key_code
           << " modifiers=" << event.modifiers
           << " editable=" << event.focus_on_editable_field;
    DiagnosticLog(stream.str());
  }
  return false;
}

bool BrowserClient::OnKeyEvent(CefRefPtr<CefBrowser> browser,
                               const CefKeyEvent& event,
                               CefEventHandle os_event) {
  static std::atomic<int> key_event_count{0};
  if (ShouldDiagnosticLog(key_event_count, 80, 200)) {
    std::ostringstream stream;
    stream << "BrowserClient::OnKeyEvent browser_id="
           << (browser ? browser->GetIdentifier() : -1)
           << " type=" << event.type
           << " vk=" << event.windows_key_code
           << " native=" << event.native_key_code
           << " modifiers=" << event.modifiers
           << " editable=" << event.focus_on_editable_field;
    DiagnosticLog(stream.str());
  }
  return false;
}

void BrowserClient::OnTakeFocus(CefRefPtr<CefBrowser> browser, bool next) {
  DiagnosticLog("BrowserClient::OnTakeFocus browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1) +
                " next=" + (next ? "true" : "false"));
  if (delegate_) {
    delegate_->OnTakeFocusRequest(next);
  }
}

bool BrowserClient::OnSetFocus(CefRefPtr<CefBrowser> browser,
                               FocusSource source) {
  DiagnosticLog("BrowserClient::OnSetFocus browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1) +
                " source=" + std::to_string(static_cast<int>(source)));
  if (delegate_) {
    delegate_->OnSetFocusRequest();
  }
  return false;
}

void BrowserClient::OnGotFocus(CefRefPtr<CefBrowser> browser) {
  DiagnosticLog("BrowserClient::OnGotFocus browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1));
}

}  // namespace offscreen
