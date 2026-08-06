#include "browser/osr_render_handler.h"

#include <algorithm>
#include <sstream>

#include "app/diagnostic_log.h"
#include "browser/osr_render_log.h"
#include "browser/render_stats.h"

namespace offscreen {

OsrRenderHandler::OsrRenderHandler(
    BrowserViewRect view_rect,
    double device_scale_factor,
    std::shared_ptr<BrowserFrame> frame,
    PaintUpdateCallback paint_update_callback,
    std::shared_ptr<RenderStats> render_stats)
    : view_rect_(view_rect),
      device_scale_factor_(NormalizeDeviceScaleFactor(device_scale_factor)),
      frame_(std::move(frame)),
      render_stats_(std::move(render_stats)),
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
  OsrRenderLogRecord record;
  record.event = "SetViewRect";
  record.x = view_rect_.x;
  record.y = view_rect_.y;
  record.w = view_rect_.width;
  record.h = view_rect_.height;
  record.scale = device_scale_factor_;
  OsrRenderLogWrite(record);
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

void OsrRenderHandler::SetStartDraggingCallback(
    StartDraggingCallback callback) {
  DiagnosticLog("OsrRenderHandler::SetStartDraggingCallback");
  start_dragging_callback_ = std::move(callback);
}

void OsrRenderHandler::SetUpdateDragCursorCallback(
    UpdateDragCursorCallback callback) {
  DiagnosticLog("OsrRenderHandler::SetUpdateDragCursorCallback");
  update_drag_cursor_callback_ = std::move(callback);
}

void OsrRenderHandler::GetViewRect(CefRefPtr<CefBrowser> browser,
                                    CefRect& rect) {
  std::lock_guard<std::mutex> lock(mutex_);
  rect = CefRect(view_rect_.x, view_rect_.y, view_rect_.width, view_rect_.height);
  OsrRenderLogRecord record;
  record.event = "GetViewRect";
  record.browser_id = browser ? browser->GetIdentifier() : -1;
  record.x = rect.x;
  record.y = rect.y;
  record.w = rect.width;
  record.h = rect.height;
  OsrRenderLogWrite(record);
}

bool OsrRenderHandler::GetScreenInfo(CefRefPtr<CefBrowser> browser,
                                      CefScreenInfo& screen_info) {
  std::lock_guard<std::mutex> lock(mutex_);
  screen_info.device_scale_factor = static_cast<float>(device_scale_factor_);
  screen_info.rect = CefRect(view_rect_.x, view_rect_.y, view_rect_.width,
                              view_rect_.height);
  screen_info.available_rect = screen_info.rect;
  OsrRenderLogRecord record;
  record.event = "GetScreenInfo";
  record.browser_id = browser ? browser->GetIdentifier() : -1;
  record.x = screen_info.rect.x;
  record.y = screen_info.rect.y;
  record.w = screen_info.rect.width;
  record.h = screen_info.rect.height;
  record.scale = screen_info.device_scale_factor;
  OsrRenderLogWrite(record);
  return true;
}

void OsrRenderHandler::OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) {
  OsrRenderLogRecord record;
  record.event = "OnPopupShow";
  record.browser_id = browser ? browser->GetIdentifier() : -1;
  record.show = show ? "true" : "false";
  OsrRenderLogWrite(record);
  if (frame_) {
    frame_->SetPopupVisible(show);
  }
  if (!show && paint_update_callback_) {
    paint_update_callback_({});
  }
}

void OsrRenderHandler::OnPopupSize(CefRefPtr<CefBrowser> browser,
                                    const CefRect& rect) {
  OsrRenderLogRecord record;
  record.event = "OnPopupSize";
  record.browser_id = browser ? browser->GetIdentifier() : -1;
  record.x = rect.x;
  record.y = rect.y;
  record.w = rect.width;
  record.h = rect.height;
  OsrRenderLogWrite(record);
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

  int64_t dirty_area_px = 0;
  for (const auto& r : dirtyRects) {
    dirty_area_px += static_cast<int64_t>(r.width) * r.height;
  }
  if (render_stats_) {
    render_stats_->OnPaintBegin(width, height,
                                static_cast<int>(dirtyRects.size()),
                                dirty_area_px, type == PET_POPUP);
  }

  OsrRenderLogRecord record;
  record.event = "OnPaint";
  record.browser_id = browser ? browser->GetIdentifier() : -1;
  record.w = width;
  record.h = height;
  record.scale = scale;
  record.type = type == PET_VIEW ? "PET_VIEW" : "PET_POPUP";
  record.dirty_count = static_cast<int>(dirtyRects.size());
  record.dirty_area_px = dirty_area_px;
  if (!dirtyRects.empty()) {
    const auto& first = dirtyRects.front();
    std::ostringstream detail;
    detail << "first_dirty=" << first.x << "," << first.y << " "
           << first.width << "x" << first.height;
    record.detail = detail.str();
  }
  OsrRenderLogWrite(record);

  std::vector<BrowserViewRect> dip_rects;

  if (type == PET_VIEW) {
    if (render_stats_) {
      render_stats_->OnSetViewImageBegin();
    }
    frame_->SetViewImage(buffer, width, height, scale);
    if (render_stats_) {
      render_stats_->OnSetViewImageDone();
    }
    for (const auto& r : dirtyRects) {
      BrowserPhysicalRect phys{r.x, r.y, r.width, r.height};
      dip_rects.push_back(PhysicalRectToDipUpdateRect(phys, scale));
    }
  } else if (type == PET_POPUP) {
    if (render_stats_) {
      render_stats_->OnSetViewImageBegin();
    }
    frame_->SetPopupImage(buffer, width, height, scale);
    if (render_stats_) {
      render_stats_->OnSetViewImageDone();
    }
    dip_rects.push_back({});
  } else {
    return;
  }

  if (paint_update_callback_) {
    paint_update_callback_(dip_rects);
  }

  if (render_stats_) {
    render_stats_->OnPaintEnd();
  }
}

void OsrRenderHandler::OnAcceleratedPaint(CefRefPtr<CefBrowser> browser,
                                          PaintElementType type,
                                          const RectList& dirtyRects,
                                          void* shared_handle) {
  double scale;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    scale = device_scale_factor_;
  }

  int width = 0;
  int height = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    width = view_rect_.width;
    height = view_rect_.height;
  }

  int64_t dirty_area_px = 0;
  for (const auto& r : dirtyRects) {
    dirty_area_px += static_cast<int64_t>(r.width) * r.height;
  }
  if (render_stats_) {
    render_stats_->OnAcceleratedPaintBegin(
        width, height, static_cast<int>(dirtyRects.size()), dirty_area_px,
        type == PET_POPUP);
  }

  OsrRenderLogRecord record;
  record.event = "OnAcceleratedPaint";
  record.browser_id = browser ? browser->GetIdentifier() : -1;
  record.w = width;
  record.h = height;
  record.scale = scale;
  record.type = type == PET_VIEW ? "ACCELERATED_VIEW" : "ACCELERATED_POPUP";
  record.dirty_count = static_cast<int>(dirtyRects.size());
  record.dirty_area_px = dirty_area_px;
  {
    std::ostringstream detail;
    detail << "shared_handle=" << HexValue(
        reinterpret_cast<uintptr_t>(shared_handle));
    record.detail = detail.str();
  }
  OsrRenderLogWrite(record);

  // 共享纹理路径下帧内容位于 GPU 侧；本采集点只统计与日志，不更新
  // BrowserFrame（CPU buffer）。Qt 侧如需显示需实现 D3D11 纹理读取
  // （OpenSharedResource），属于 GPU upload 原型，另行评估。
  std::vector<BrowserViewRect> dip_rects;
  for (const auto& r : dirtyRects) {
    BrowserPhysicalRect phys{r.x, r.y, r.width, r.height};
    dip_rects.push_back(PhysicalRectToDipUpdateRect(phys, scale));
  }
  if (paint_update_callback_) {
    paint_update_callback_(dip_rects);
  }

  if (render_stats_) {
    render_stats_->OnPaintEnd();
  }
}

bool OsrRenderHandler::StartDragging(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefDragData> drag_data,
                                     DragOperationsMask allowed_ops,
                                     int x,
                                     int y) {
  std::ostringstream stream;
  stream << "OsrRenderHandler::StartDragging browser_id="
         << (browser ? browser->GetIdentifier() : -1)
         << " has_drag_data=" << (drag_data ? "true" : "false")
         << " allowed_ops=" << allowed_ops << " screen=" << x << "," << y
         << " has_callback="
         << (start_dragging_callback_ ? "true" : "false");
  DiagnosticLog(stream.str());

  if (!start_dragging_callback_) {
    return false;
  }
  return start_dragging_callback_(browser, drag_data, allowed_ops, x, y);
}

void OsrRenderHandler::UpdateDragCursor(CefRefPtr<CefBrowser> browser,
                                        DragOperation operation) {
  DiagnosticLog("OsrRenderHandler::UpdateDragCursor browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1) +
                " operation=" + std::to_string(operation) +
                " has_callback=" +
                (update_drag_cursor_callback_ ? "true" : "false"));
  if (update_drag_cursor_callback_) {
    update_drag_cursor_callback_(browser, operation);
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
