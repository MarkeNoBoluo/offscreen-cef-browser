#include "qt/browser_widget.h"

#include <utility>

#include <QResizeEvent>

namespace offscreen {

BrowserWidget::BrowserWidget(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_NativeWindow, true);
  setAttribute(Qt::WA_DontCreateNativeAncestors, false);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
}

HWND BrowserWidget::NativeParentHandle() const {
  return reinterpret_cast<HWND>(winId());
}

BrowserViewRect BrowserWidget::CurrentViewRect() const {
  return ViewRectFromWidgetSize(width(), height());
}

void BrowserWidget::SetResizeCallback(ResizeCallback resize_callback) {
  resize_callback_ = std::move(resize_callback);
}

void BrowserWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (resize_callback_) {
    resize_callback_(CurrentViewRect());
  }
}

}  // namespace offscreen
