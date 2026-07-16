#pragma once

#include <memory>
#include <mutex>

#include <QImage>

#include "browser/browser_geometry.h"
#include "browser/browser_paint_geometry.h"

namespace offscreen {

struct BrowserFrameSnapshot {
  bool has_view = false;
  bool popup_visible = false;
  BrowserViewRect popup_rect;
  QImage view_image;
  QImage popup_image;
};

class BrowserFrame {
 public:
  BrowserFrame() = default;

  void SetViewImage(const void* bgra_buffer, int width, int height, double scale);
  void SetPopupImage(const void* bgra_buffer, int width, int height, double scale);
  void SetPopupVisible(bool visible);
  void SetPopupRect(BrowserViewRect rect);
  BrowserFrameSnapshot Snapshot() const;

 private:
  mutable std::mutex mutex_;
  QImage view_image_;
  QImage popup_image_;
  bool popup_visible_ = false;
  BrowserViewRect popup_rect_;
};

}  // namespace offscreen
