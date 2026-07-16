#include "browser/minimal_render_handler.h"

namespace offscreen {

MinimalRenderHandler::MinimalRenderHandler(BrowserViewRect view_rect)
    : view_rect_(view_rect) {}

void MinimalRenderHandler::SetViewRect(BrowserViewRect view_rect) {
  view_rect_ = view_rect;
}

BrowserViewRect MinimalRenderHandler::view_rect() const {
  return view_rect_;
}

void MinimalRenderHandler::GetViewRect(CefRefPtr<CefBrowser> browser,
                                       CefRect& rect) {
  rect = CefRect(view_rect_.x, view_rect_.y, view_rect_.width, view_rect_.height);
}

bool MinimalRenderHandler::GetScreenInfo(CefRefPtr<CefBrowser> browser,
                                         CefScreenInfo& screen_info) {
  screen_info.device_scale_factor = 1.0f;
  screen_info.rect = CefRect(view_rect_.x, view_rect_.y, view_rect_.width,
                             view_rect_.height);
  screen_info.available_rect = screen_info.rect;
  return true;
}

void MinimalRenderHandler::OnPaint(CefRefPtr<CefBrowser> browser,
                                   PaintElementType type,
                                   const RectList& dirtyRects,
                                   const void* buffer,
                                   int width,
                                   int height) {}

}  // namespace offscreen
