#include "browser/browser_client.h"

#include <atomic>
#include <sstream>

#include "app/diagnostic_log.h"
#include "include/cef_download_item.h"
#include "include/cef_frame.h"

namespace offscreen {

namespace {

/// 将 CEF UTF-16/UTF-8 抽象字符串转换为宿主 UTF-8 文本。
/// @param value CEF 字符串。
/// @return 标准 UTF-8 字符串。
std::string CefStringToUtf8(const CefString& value) {
  return value.ToString();
}

/// ERR_ABORTED (-3) 表示导航被新请求替换，CEF 会附带触发一次 OnLoadError；
/// 这类取消错误不向宿主转发，避免正常跳转被误报为加载失败。
constexpr int kCefErrAborted = -3;

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

CefRefPtr<CefContextMenuHandler> BrowserClient::GetContextMenuHandler() {
  return this;
}

CefRefPtr<CefDownloadHandler> BrowserClient::GetDownloadHandler() {
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
  if (static_cast<int>(errorCode) == kCefErrAborted) {
    DiagnosticLog("BrowserClient::OnLoadError ignored ERR_ABORTED");
    return;
  }
  if (delegate_) {
    delegate_->OnLoadError(static_cast<int>(errorCode),
                           CefStringToUtf8(failedUrl),
                           CefStringToUtf8(errorText));
  }
}

void BrowserClient::OnBeforeContextMenu(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params,
    CefRefPtr<CefMenuModel> model) {
  (void)frame;
  DiagnosticLog("BrowserClient::OnBeforeContextMenu browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1));
  // OSR 下 CEF 无法绘制原生菜单；清空默认模型以抑制残留菜单，由宿主自绘。
  if (model) {
    model->Clear();
  }
  if (delegate_ && params) {
    delegate_->OnContextMenuRequested(params->GetXCoord(),
                                      params->GetYCoord());
  }
}

void BrowserClient::OnBeforeDownload(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDownloadItem> download_item,
    const CefString& suggested_name,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  (void)download_item;
  DiagnosticLog("BrowserClient::OnBeforeDownload browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1) +
                " suggested_name=[" + CefStringToUtf8(suggested_name) + "]");
  if (delegate_) {
    delegate_->OnDownloadStarted(callback, CefStringToUtf8(suggested_name));
  }
}

void BrowserClient::OnDownloadUpdated(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDownloadItem> download_item,
    CefRefPtr<CefDownloadItemCallback> callback) {
  (void)callback;
  if (!download_item) {
    return;
  }
  int state = -1;
  if (download_item->IsComplete()) {
    state = 1;
  } else if (download_item->IsCanceled()) {
    state = 2;
  } else {
    return;
  }
  const std::string file_name =
      CefStringToUtf8(download_item->GetSuggestedFileName());
  const std::string full_path = CefStringToUtf8(download_item->GetFullPath());
  DiagnosticLog("BrowserClient::OnDownloadUpdated browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1) +
                " state=" + std::to_string(state) + " name=[" + file_name +
                "] path=[" + full_path + "]");
  if (delegate_) {
    delegate_->OnDownloadStateChanged(state, file_name, full_path);
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

void BrowserClient::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> frame,
                                    const CefString& url) {
  if (!frame || !frame->IsMain()) {
    return;
  }
  DiagnosticLog("BrowserClient::OnAddressChange browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1) +
                " url=[" + CefStringToUtf8(url) + "]");
  if (delegate_) {
    delegate_->OnAddressChanged(CefStringToUtf8(url));
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

bool BrowserClient::OnBeforePopup(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const CefString& target_url,
    const CefString& target_frame_name,
    CefLifeSpanHandler::WindowOpenDisposition target_disposition,
    bool user_gesture,
    const CefPopupFeatures& popupFeatures,
    CefWindowInfo& windowInfo,
    CefRefPtr<CefClient>& client,
    CefBrowserSettings& settings,
    CefRefPtr<CefDictionaryValue>& extra_info,
    bool* no_javascript_access) {
  std::string url = target_url.ToString();
  DiagnosticLog("BrowserClient::OnBeforePopup browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1) +
                " url=[" + url + "]");
  if (url.empty()) {
    url = "about:blank";
  }
  if (delegate_) {
    delegate_->OnPopupRequest(url);
  }
  return true;
}

bool BrowserClient::OnOpenURLFromTab(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const CefString& target_url,
    CefRequestHandler::WindowOpenDisposition target_disposition,
    bool user_gesture) {
  std::string url = target_url.ToString();
  DiagnosticLog("BrowserClient::OnOpenURLFromTab browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1) +
                " url=[" + url + "] disposition=" +
                std::to_string(static_cast<int>(target_disposition)));
  if (url.empty()) {
    url = "about:blank";
  }
  if (delegate_) {
    delegate_->OnPopupRequest(url);
  }
  return true;
}

}  // namespace offscreen
