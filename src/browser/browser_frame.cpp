#include "browser/browser_frame.h"

#include <atomic>
#include <cstring>
#include <sstream>

#include "app/diagnostic_log.h"

namespace offscreen {

void BrowserFrame::SetViewImage(const void* bgra_buffer,
                                 int width,
                                 int height,
                                 double scale) {
  if (!bgra_buffer || width <= 0 || height <= 0) {
    DiagnosticLog("BrowserFrame::SetViewImage ignored invalid input");
    return;
  }
  static std::atomic<int> view_image_count{0};
  if (ShouldDiagnosticLog(view_image_count, 40, 100)) {
    std::ostringstream stream;
    stream << "BrowserFrame::SetViewImage size=" << width << "x" << height
           << " scale=" << scale << " buffer="
           << HexValue(reinterpret_cast<uintptr_t>(bgra_buffer));
    DiagnosticLog(stream.str());
  }
  QImage image(static_cast<const uchar*>(bgra_buffer), width, height,
               QImage::Format_ARGB32);
  QImage copy = image.copy();
  copy.setDevicePixelRatio(NormalizeDeviceScaleFactor(scale));
  std::lock_guard<std::mutex> lock(mutex_);
  view_image_ = std::move(copy);
}

void BrowserFrame::SetPopupImage(const void* bgra_buffer,
                                  int width,
                                  int height,
                                  double scale) {
  if (!bgra_buffer || width <= 0 || height <= 0) {
    DiagnosticLog("BrowserFrame::SetPopupImage ignored invalid input");
    return;
  }
  static std::atomic<int> popup_image_count{0};
  if (ShouldDiagnosticLog(popup_image_count, 20, 100)) {
    std::ostringstream stream;
    stream << "BrowserFrame::SetPopupImage size=" << width << "x" << height
           << " scale=" << scale << " buffer="
           << HexValue(reinterpret_cast<uintptr_t>(bgra_buffer));
    DiagnosticLog(stream.str());
  }
  QImage image(static_cast<const uchar*>(bgra_buffer), width, height,
               QImage::Format_ARGB32);
  QImage copy = image.copy();
  copy.setDevicePixelRatio(NormalizeDeviceScaleFactor(scale));
  std::lock_guard<std::mutex> lock(mutex_);
  popup_image_ = std::move(copy);
}

void BrowserFrame::SetPopupVisible(bool visible) {
  DiagnosticLog("BrowserFrame::SetPopupVisible visible=" +
                std::string(visible ? "true" : "false"));
  std::lock_guard<std::mutex> lock(mutex_);
  popup_visible_ = visible;
}

void BrowserFrame::SetPopupRect(BrowserViewRect rect) {
  std::ostringstream stream;
  stream << "BrowserFrame::SetPopupRect rect=" << rect.x << "," << rect.y
         << " " << rect.width << "x" << rect.height;
  DiagnosticLog(stream.str());
  std::lock_guard<std::mutex> lock(mutex_);
  popup_rect_ = rect;
}

BrowserFrameSnapshot BrowserFrame::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  BrowserFrameSnapshot snapshot;
  snapshot.popup_visible = popup_visible_;
  snapshot.popup_rect = popup_rect_;
  if (!view_image_.isNull()) {
    snapshot.has_view = true;
    snapshot.view_image = view_image_.copy();
  }
  if (!popup_image_.isNull()) {
    snapshot.popup_image = popup_image_.copy();
  }
  return snapshot;
}

}  // namespace offscreen
