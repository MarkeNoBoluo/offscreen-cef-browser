#include "qt/browser_widget.h"

#include <algorithm>
#include <atomic>
#include <sstream>
#include <utility>

#include "app/diagnostic_log.h"
#include <QMetaObject>
#include <QPainter>
#include <QPaintEvent>
#include <QRegion>
#include <QResizeEvent>
#include <QScreen>
#include <QWindow>

#include "browser/browser_frame.h"

namespace offscreen {

BrowserWidget::BrowserWidget(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_NativeWindow, true);
  setAttribute(Qt::WA_DontCreateNativeAncestors, false);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setAutoFillBackground(false);
  DiagnosticLog("BrowserWidget constructed");
}

HWND BrowserWidget::NativeParentHandle() const {
  const HWND handle = reinterpret_cast<HWND>(winId());
  DiagnosticLog("BrowserWidget::NativeParentHandle hwnd=" +
                HexValue(reinterpret_cast<uintptr_t>(handle)));
  return handle;
}

BrowserViewRect BrowserWidget::CurrentViewRect() const {
  return ViewRectFromWidgetSize(width(), height());
}

double BrowserWidget::CurrentDeviceScaleFactor() const {
  double scale = 1.0;
  if (windowHandle() && windowHandle()->screen()) {
    scale = windowHandle()->screen()->devicePixelRatio();
  } else {
    scale = devicePixelRatioF();
  }
  return NormalizeDeviceScaleFactor(scale);
}

void BrowserWidget::SetFrame(std::shared_ptr<BrowserFrame> frame) {
  frame_ = std::move(frame);
  DiagnosticLog("BrowserWidget::SetFrame frame=" +
                HexValue(reinterpret_cast<uintptr_t>(frame_.get())));
}

void BrowserWidget::SetResizeCallback(ResizeCallback resize_callback) {
  resize_callback_ = std::move(resize_callback);
  DiagnosticLog("BrowserWidget::SetResizeCallback");
}

void BrowserWidget::ScheduleFrameUpdate(
    const std::vector<BrowserViewRect>& dirty_rects) {
  static std::atomic<int> schedule_count{0};
  if (ShouldDiagnosticLog(schedule_count, 40, 100)) {
    std::ostringstream stream;
    stream << "BrowserWidget::ScheduleFrameUpdate dirty_count="
           << dirty_rects.size();
    if (!dirty_rects.empty()) {
      const auto& first = dirty_rects.front();
      stream << " first_dirty=" << first.x << "," << first.y << " "
             << first.width << "x" << first.height;
    }
    DiagnosticLog(stream.str());
  }
  if (dirty_rects.empty()) {
    QMetaObject::invokeMethod(this, [this]() { update(); },
                              Qt::QueuedConnection);
    return;
  }
  QRegion region;
  for (const auto& r : dirty_rects) {
    region += QRect(r.x, r.y, r.width, r.height);
  }
  QMetaObject::invokeMethod(this, [this, region]() { update(region); },
                            Qt::QueuedConnection);
}

void BrowserWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  static std::atomic<int> resize_event_count{0};
  if (ShouldDiagnosticLog(resize_event_count, 20, 50)) {
    std::ostringstream stream;
    stream << "BrowserWidget::resizeEvent widget=" << width() << "x"
           << height() << " scale=" << CurrentDeviceScaleFactor()
           << " has_resize_callback="
           << (resize_callback_ ? "true" : "false");
    DiagnosticLog(stream.str());
  }
  if (resize_callback_) {
    resize_callback_(CurrentViewRect(), CurrentDeviceScaleFactor());
  }
}

void BrowserWidget::paintEvent(QPaintEvent* event) {
  QPainter painter(this);
  static std::atomic<int> paint_event_count{0};

  if (!frame_) {
    if (ShouldDiagnosticLog(paint_event_count, 40, 100)) {
      DiagnosticLog("BrowserWidget::paintEvent frame=null event_rect=" +
                    std::to_string(event->rect().x()) + "," +
                    std::to_string(event->rect().y()) + " " +
                    std::to_string(event->rect().width()) + "x" +
                    std::to_string(event->rect().height()));
    }
    painter.fillRect(rect(), QColor(240, 240, 240));
    return;
  }

  BrowserFrameSnapshot snapshot = frame_->Snapshot();
  if (ShouldDiagnosticLog(paint_event_count, 40, 100)) {
    std::ostringstream stream;
    stream << "BrowserWidget::paintEvent has_view="
           << (snapshot.has_view ? "true" : "false")
           << " event_rect=" << event->rect().x() << "," << event->rect().y()
           << " " << event->rect().width() << "x" << event->rect().height()
           << " widget=" << width() << "x" << height();
    if (snapshot.has_view) {
      stream << " view_image=" << snapshot.view_image.width() << "x"
             << snapshot.view_image.height() << " dpr="
             << snapshot.view_image.devicePixelRatio();
    }
    stream << " popup_visible="
           << (snapshot.popup_visible ? "true" : "false")
           << " popup_image_null="
           << (snapshot.popup_image.isNull() ? "true" : "false");
    DiagnosticLog(stream.str());
  }

  if (!snapshot.has_view) {
    painter.fillRect(rect(), QColor(240, 240, 240));
    return;
  }

  painter.drawImage(QPoint(0, 0), snapshot.view_image);

  if (snapshot.popup_visible && !snapshot.popup_image.isNull()) {
    painter.drawImage(QPoint(snapshot.popup_rect.x, snapshot.popup_rect.y),
                      snapshot.popup_image);
  }
}

}  // namespace offscreen
