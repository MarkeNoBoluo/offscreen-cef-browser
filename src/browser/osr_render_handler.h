#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "browser/browser_frame.h"
#include "browser/browser_paint_geometry.h"
#include "include/cef_render_handler.h"

namespace offscreen {

using PaintUpdateCallback =
    std::function<void(const std::vector<BrowserViewRect>&)>;

class OsrRenderHandler final : public CefRenderHandler {
 public:
  OsrRenderHandler(BrowserViewRect view_rect,
                   double device_scale_factor,
                   std::shared_ptr<BrowserFrame> frame,
                   PaintUpdateCallback paint_update_callback);

  void SetViewRect(BrowserViewRect view_rect, double device_scale_factor);
  BrowserViewRect view_rect() const;

  void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
  bool GetScreenInfo(CefRefPtr<CefBrowser> browser,
                     CefScreenInfo& screen_info) override;
  void OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) override;
  void OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect& rect) override;
  void OnPaint(CefRefPtr<CefBrowser> browser,
               PaintElementType type,
               const RectList& dirtyRects,
               const void* buffer,
               int width,
               int height) override;

 private:
  mutable std::mutex mutex_;
  BrowserViewRect view_rect_;
  double device_scale_factor_ = 1.0;
  std::shared_ptr<BrowserFrame> frame_;
  PaintUpdateCallback paint_update_callback_;

  IMPLEMENT_REFCOUNTING(OsrRenderHandler);
};

}  // namespace offscreen
