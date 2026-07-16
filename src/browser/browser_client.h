#pragma once

#include <string>

#include "browser/minimal_render_handler.h"
#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_request_handler.h"

namespace offscreen {

class BrowserClient final : public CefClient,
                            public CefLifeSpanHandler,
                            public CefLoadHandler,
                            public CefDisplayHandler,
                            public CefRequestHandler {
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
    virtual void OnTitleChanged(const std::string& title) = 0;
    virtual void OnLoadErrorText(const std::string& error_text) = 0;
    virtual void OnRenderProcessTerminated() = 0;
  };

  BrowserClient(Delegate* delegate,
                CefRefPtr<MinimalRenderHandler> render_handler);

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
  CefRefPtr<CefLoadHandler> GetLoadHandler() override;
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;
  CefRefPtr<CefRenderHandler> GetRenderHandler() override;
  CefRefPtr<CefRequestHandler> GetRequestHandler() override;

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
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status) override;

 private:
  Delegate* delegate_ = nullptr;
  CefRefPtr<MinimalRenderHandler> render_handler_;

  IMPLEMENT_REFCOUNTING(BrowserClient);
};

}  // namespace offscreen
