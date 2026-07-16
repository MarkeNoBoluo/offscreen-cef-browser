#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <QWidget>
#include <windows.h>

#include "browser/browser_geometry.h"
#include "browser/browser_paint_geometry.h"

namespace offscreen {

class BrowserFrame;

class BrowserWidget final : public QWidget {
 public:
  using ResizeCallback =
      std::function<void(BrowserViewRect, double device_scale_factor)>;

  explicit BrowserWidget(QWidget* parent = nullptr);

  HWND NativeParentHandle() const;
  BrowserViewRect CurrentViewRect() const;
  double CurrentDeviceScaleFactor() const;
  void SetFrame(std::shared_ptr<BrowserFrame> frame);
  void SetResizeCallback(ResizeCallback resize_callback);
  void ScheduleFrameUpdate(const std::vector<BrowserViewRect>& dirty_rects);

 protected:
  void resizeEvent(QResizeEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

 private:
  ResizeCallback resize_callback_;
  std::shared_ptr<BrowserFrame> frame_;
};

}  // namespace offscreen
