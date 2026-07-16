#pragma once

#include "browser/browser_geometry.h"
#include "include/cef_render_handler.h"

namespace offscreen {

class MinimalRenderHandler final : public CefRenderHandler {
 public:
  explicit MinimalRenderHandler(BrowserViewRect view_rect);

  void SetViewRect(BrowserViewRect view_rect);
  BrowserViewRect view_rect() const;

  void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
  bool GetScreenInfo(CefRefPtr<CefBrowser> browser,
                     CefScreenInfo& screen_info) override;
  void OnPaint(CefRefPtr<CefBrowser> browser,
               PaintElementType type,
               const RectList& dirtyRects,
               const void* buffer,
               int width,
               int height) override;

 private:
  BrowserViewRect view_rect_;

  IMPLEMENT_REFCOUNTING(MinimalRenderHandler);
};

}  // namespace offscreen
