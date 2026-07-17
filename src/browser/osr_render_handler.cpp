#include "browser/osr_render_handler.h"

#include <algorithm>
#include <atomic>
#include <sstream>

#include "app/diagnostic_log.h"

namespace offscreen {

OsrRenderHandler::OsrRenderHandler(
    BrowserViewRect view_rect,
    double device_scale_factor,
    std::shared_ptr<BrowserFrame> frame,
    PaintUpdateCallback paint_update_callback)
    : view_rect_(view_rect),
      device_scale_factor_(NormalizeDeviceScaleFactor(device_scale_factor)),
      frame_(std::move(frame)),
      paint_update_callback_(std::move(paint_update_callback)) {
  std::ostringstream stream;
  stream << "OsrRenderHandler constructed rect=" << view_rect_.x << ","
         << view_rect_.y << " " << view_rect_.width << "x"
         << view_rect_.height << " scale=" << device_scale_factor_
         << " frame=" << HexValue(reinterpret_cast<uintptr_t>(frame_.get()))
         << " has_paint_callback="
         << (paint_update_callback_ ? "true" : "false");
  DiagnosticLog(stream.str());
}

void OsrRenderHandler::SetViewRect(BrowserViewRect view_rect,
                                    double device_scale_factor) {
  std::lock_guard<std::mutex> lock(mutex_);
  view_rect_ = view_rect;
  device_scale_factor_ = NormalizeDeviceScaleFactor(device_scale_factor);
  static std::atomic<int> set_rect_count{0};
  if (ShouldDiagnosticLog(set_rect_count, 20, 50)) {
    std::ostringstream stream;
    stream << "OsrRenderHandler::SetViewRect rect=" << view_rect_.x << ","
           << view_rect_.y << " " << view_rect_.width << "x"
           << view_rect_.height << " scale=" << device_scale_factor_;
    DiagnosticLog(stream.str());
  }
}

BrowserViewRect OsrRenderHandler::view_rect() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return view_rect_;
}

void OsrRenderHandler::SetImeCompositionRangeChangedCallback(
    ImeCompositionRangeChangedCallback callback) {
  DiagnosticLog("OsrRenderHandler::SetImeCompositionRangeChangedCallback");
  ime_composition_range_changed_callback_ = std::move(callback);
}

void OsrRenderHandler::GetViewRect(CefRefPtr<CefBrowser> browser,
                                    CefRect& rect) {
  std::lock_guard<std::mutex> lock(mutex_);
  rect = CefRect(view_rect_.x, view_rect_.y, view_rect_.width, view_rect_.height);
  static std::atomic<int> get_view_rect_count{0};
  if (ShouldDiagnosticLog(get_view_rect_count, 20, 100)) {
    std::ostringstream stream;
    stream << "OsrRenderHandler::GetViewRect browser_id="
           << (browser ? browser->GetIdentifier() : -1) << " rect=" << rect.x
           << "," << rect.y << " " << rect.width << "x" << rect.height;
    DiagnosticLog(stream.str());
  }
}

bool OsrRenderHandler::GetScreenInfo(CefRefPtr<CefBrowser> browser,
                                      CefScreenInfo& screen_info) {
  std::lock_guard<std::mutex> lock(mutex_);
  screen_info.device_scale_factor = static_cast<float>(device_scale_factor_);
  screen_info.rect = CefRect(view_rect_.x, view_rect_.y, view_rect_.width,
                              view_rect_.height);
  screen_info.available_rect = screen_info.rect;
  static std::atomic<int> screen_info_count{0};
  if (ShouldDiagnosticLog(screen_info_count, 20, 100)) {
    std::ostringstream stream;
    stream << "OsrRenderHandler::GetScreenInfo browser_id="
           << (browser ? browser->GetIdentifier() : -1)
           << " device_scale_factor=" << screen_info.device_scale_factor
           << " rect=" << screen_info.rect.x << "," << screen_info.rect.y
           << " " << screen_info.rect.width << "x"
           << screen_info.rect.height;
    DiagnosticLog(stream.str());
  }
  return true;
}

void OsrRenderHandler::OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) {
  DiagnosticLog("OsrRenderHandler::OnPopupShow browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1) +
                " show=" + (show ? "true" : "false"));
  if (frame_) {
    frame_->SetPopupVisible(show);
  }
  if (!show && paint_update_callback_) {
    paint_update_callback_({});
  }
}

void OsrRenderHandler::OnPopupSize(CefRefPtr<CefBrowser> browser,
                                    const CefRect& rect) {
  std::ostringstream stream;
  stream << "OsrRenderHandler::OnPopupSize browser_id="
         << (browser ? browser->GetIdentifier() : -1) << " rect=" << rect.x
         << "," << rect.y << " " << rect.width << "x" << rect.height;
  DiagnosticLog(stream.str());
  if (frame_) {
    frame_->SetPopupRect(
        BrowserViewRect{rect.x, rect.y, rect.width, rect.height});
  }
}

void OsrRenderHandler::OnPaint(CefRefPtr<CefBrowser> browser,
                                PaintElementType type,
                                const RectList& dirtyRects,
                                const void* buffer,
                                int width,
                                int height) {
  if (!frame_ || !buffer || width <= 0 || height <= 0) {
    DiagnosticLog("OsrRenderHandler::OnPaint ignored invalid input frame=" +
                  HexValue(reinterpret_cast<uintptr_t>(frame_.get())) +
                  " buffer=" +
                  HexValue(reinterpret_cast<uintptr_t>(buffer)) +
                  " width=" + std::to_string(width) +
                  " height=" + std::to_string(height));
    return;
  }

  double scale;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    scale = device_scale_factor_;
  }

  std::vector<BrowserViewRect> dip_rects;
  static std::atomic<int> paint_count{0};
  if (ShouldDiagnosticLog(paint_count, 40, 100)) {
    std::ostringstream stream;
    stream << "OsrRenderHandler::OnPaint browser_id="
           << (browser ? browser->GetIdentifier() : -1)
           << " type=" << (type == PET_VIEW ? "PET_VIEW" : "PET_POPUP")
           << " size=" << width << "x" << height << " scale=" << scale
           << " dirty_count=" << dirtyRects.size();
    if (!dirtyRects.empty()) {
      const auto& first = dirtyRects.front();
      stream << " first_dirty=" << first.x << "," << first.y << " "
             << first.width << "x" << first.height;
    }
    DiagnosticLog(stream.str());
  }

  if (type == PET_VIEW) {
    frame_->SetViewImage(buffer, width, height, scale);
    for (const auto& r : dirtyRects) {
      BrowserPhysicalRect phys{r.x, r.y, r.width, r.height};
      dip_rects.push_back(PhysicalRectToDipUpdateRect(phys, scale));
    }
  } else if (type == PET_POPUP) {
    frame_->SetPopupImage(buffer, width, height, scale);
    dip_rects.push_back({});
  } else {
    return;
  }

  if (paint_update_callback_) {
    paint_update_callback_(dip_rects);
  }
}

void OsrRenderHandler::OnImeCompositionRangeChanged(
    CefRefPtr<CefBrowser> browser,
    const CefRange& selected_range,
    const RectList& character_bounds) {
  (void)browser;
  std::ostringstream stream;
  stream << "OsrRenderHandler::OnImeCompositionRangeChanged selected="
         << selected_range.from << "-" << selected_range.to
         << " bounds=" << character_bounds.size();
  if (!character_bounds.empty()) {
    const auto& first = character_bounds.front();
    stream << " first=" << first.x << "," << first.y << " " << first.width
           << "x" << first.height;
  }
  DiagnosticLog(stream.str());
  if (ime_composition_range_changed_callback_) {
    ime_composition_range_changed_callback_(selected_range, character_bounds);
  }
}

}  // namespace offscreen
