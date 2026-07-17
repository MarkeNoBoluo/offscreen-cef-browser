#pragma once

#include <string>

#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_focus_handler.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_request_handler.h"

namespace offscreen {

class BrowserClient final : public CefClient,
                            public CefLifeSpanHandler,
                            public CefLoadHandler,
                            public CefDisplayHandler,
                            public CefRequestHandler,
                            public CefKeyboardHandler,
                            public CefFocusHandler {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void OnBrowserCreated(CefRefPtr<CefBrowser> browser) = 0;
    virtual void OnBrowserClosing(CefRefPtr<CefBrowser> browser) = 0;
    virtual void OnBrowserClosed(CefRefPtr<CefBrowser> browser) = 0;
    virtual void OnLoadStateChanged(bool is_loading,
                                    bool can_go_back,
                                    bool can_go_forward) = 0;
    virtual void OnAddressChanged(const std::string& url) = 0;
    virtual void OnTitleChanged(const std::string& title) = 0;
    virtual void OnLoadErrorText(const std::string& error_text) = 0;
    virtual void OnRenderProcessTerminated() = 0;
    virtual void OnCursorChanged(int cursor_type,
                                 CefCursorHandle cursor_handle) = 0;
    virtual void OnTakeFocusRequest(bool next) = 0;
    virtual void OnSetFocusRequest() = 0;
    virtual void OnPopupRequest(const std::string& url) = 0;
  };

  BrowserClient(Delegate* delegate,
                CefRefPtr<CefRenderHandler> render_handler);

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
  CefRefPtr<CefLoadHandler> GetLoadHandler() override;
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;
  CefRefPtr<CefRenderHandler> GetRenderHandler() override;
  CefRefPtr<CefRequestHandler> GetRequestHandler() override;
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override;
  CefRefPtr<CefFocusHandler> GetFocusHandler() override;

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                            bool isLoading,
                            bool canGoBack,
                            bool canGoForward) override;
  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode errorCode,
                   const CefString& errorText,
                   const CefString& failedUrl) override;
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  void OnAddressChange(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status) override;
  bool OnCursorChange(CefRefPtr<CefBrowser> browser,
                      CefCursorHandle cursor,
                      cef_cursor_type_t type,
                      const CefCursorInfo& custom_cursor_info) override;
  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                     const CefKeyEvent& event,
                     CefEventHandle os_event,
                     bool* is_keyboard_shortcut) override;
  bool OnKeyEvent(CefRefPtr<CefBrowser> browser,
                  const CefKeyEvent& event,
                  CefEventHandle os_event) override;
  void OnTakeFocus(CefRefPtr<CefBrowser> browser, bool next) override;
  bool OnSetFocus(CefRefPtr<CefBrowser> browser, FocusSource source) override;
  void OnGotFocus(CefRefPtr<CefBrowser> browser) override;

  // CefLifeSpanHandler:
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
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
                     bool* no_javascript_access) override;

  // CefRequestHandler:
  bool OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        const CefString& target_url,
                        CefRequestHandler::WindowOpenDisposition target_disposition,
                        bool user_gesture) override;

 private:
  Delegate* delegate_ = nullptr;
  CefRefPtr<CefRenderHandler> render_handler_;

  IMPLEMENT_REFCOUNTING(BrowserClient);
};

}  // namespace offscreen
