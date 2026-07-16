#pragma once

#include <functional>

#include <QWidget>
#include <windows.h>

#include "browser/browser_geometry.h"

namespace offscreen {

class BrowserWidget final : public QWidget {
 public:
  using ResizeCallback = std::function<void(BrowserViewRect)>;

  explicit BrowserWidget(QWidget* parent = nullptr);

  HWND NativeParentHandle() const;
  BrowserViewRect CurrentViewRect() const;
  void SetResizeCallback(ResizeCallback resize_callback);

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  ResizeCallback resize_callback_;
};

}  // namespace offscreen
