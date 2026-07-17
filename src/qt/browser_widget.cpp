#include "qt/browser_widget.h"

#include <algorithm>
#include <atomic>
#include <sstream>
#include <utility>

#include "app/diagnostic_log.h"
#include "browser/browser_frame.h"
#include "browser/browser_input_mapping.h"
#include "browser/browser_service.h"
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QRegion>
#include <QResizeEvent>
#include <QScreen>
#include <QWindow>
#include <QWheelEvent>

namespace offscreen {

namespace {

// cef_cursor_type_t values (from CEF 96 cef_types.h)
constexpr int kCursorPointer = 0;            // CT_POINTER
constexpr int kCursorCross = 1;              // CT_CROSS
constexpr int kCursorHand = 2;               // CT_HAND
constexpr int kCursorIBeam = 3;              // CT_IBEAM
constexpr int kCursorWait = 4;               // CT_WAIT
constexpr int kCursorHelp = 5;               // CT_HELP
constexpr int kCursorEastResize = 6;         // CT_EASTRESIZE
constexpr int kCursorNorthResize = 7;        // CT_NORTHRESIZE
constexpr int kCursorNorthEastResize = 8;    // CT_NORTHEASTRESIZE
constexpr int kCursorNorthWestResize = 9;    // CT_NORTHWESTRESIZE
constexpr int kCursorSouthResize = 10;       // CT_SOUTHRESIZE
constexpr int kCursorSouthEastResize = 11;   // CT_SOUTHEASTRESIZE
constexpr int kCursorSouthWestResize = 12;   // CT_SOUTHWESTRESIZE
constexpr int kCursorWestResize = 13;        // CT_WESTRESIZE
constexpr int kCursorNorthSouthResize = 14;  // CT_NORTHSOUTHRESIZE
constexpr int kCursorEastWestResize = 15;    // CT_EASTWESTRESIZE
constexpr int kCursorMove = 29;              // CT_MOVE
constexpr int kCursorVerticalText = 30;      // CT_VERTICALTEXT
constexpr int kCursorCell = 31;              // CT_CELL
constexpr int kCursorContextMenu = 32;       // CT_CONTEXTMENU
constexpr int kCursorAlias = 33;             // CT_ALIAS
constexpr int kCursorProgress = 34;          // CT_PROGRESS
constexpr int kCursorNoDrop = 35;            // CT_NODROP
constexpr int kCursorCopy = 36;              // CT_COPY
constexpr int kCursorNone = 37;              // CT_NONE
constexpr int kCursorNotAllowed = 38;        // CT_NOTALLOWED
constexpr int kCursorZoomIn = 39;            // CT_ZOOMIN
constexpr int kCursorZoomOut = 40;           // CT_ZOOMOUT
constexpr int kCursorGrab = 41;              // CT_GRAB
constexpr int kCursorGrabbing = 42;          // CT_GRABBING

}  // namespace

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

void BrowserWidget::SetBrowserService(BrowserService* service) {
  browser_service_ = service;
  DiagnosticLog("BrowserWidget::SetBrowserService service=" +
                HexValue(reinterpret_cast<uintptr_t>(service)));
}

void BrowserWidget::SetCefCursor(int cursor_type, HCURSOR cursor_handle) {
  const HWND hwnd = reinterpret_cast<HWND>(winId());
  if (cursor_handle && ::IsWindow(hwnd)) {
    ::SetClassLongPtr(
        hwnd, GCLP_HCURSOR,
        static_cast<LONG_PTR>(reinterpret_cast<uintptr_t>(cursor_handle)));
    ::SetCursor(cursor_handle);
    return;
  }

  switch (cursor_type) {
    case kCursorPointer:
      setCursor(Qt::ArrowCursor);
      break;
    case kCursorCross:
      setCursor(Qt::CrossCursor);
      break;
    case kCursorHand:
      setCursor(Qt::PointingHandCursor);
      break;
    case kCursorIBeam:
      setCursor(Qt::IBeamCursor);
      break;
    case kCursorWait:
      setCursor(Qt::WaitCursor);
      break;
    case kCursorHelp:
      setCursor(Qt::WhatsThisCursor);
      break;
    case kCursorEastResize:
    case kCursorWestResize:
    case kCursorEastWestResize:
      setCursor(Qt::SizeHorCursor);
      break;
    case kCursorNorthResize:
    case kCursorSouthResize:
    case kCursorNorthSouthResize:
      setCursor(Qt::SizeVerCursor);
      break;
    case kCursorNorthEastResize:
    case kCursorSouthWestResize:
      setCursor(Qt::SizeBDiagCursor);
      break;
    case kCursorNorthWestResize:
    case kCursorSouthEastResize:
      setCursor(Qt::SizeFDiagCursor);
      break;
    case kCursorMove:
      setCursor(Qt::SizeAllCursor);
      break;
    case kCursorVerticalText:
      setCursor(Qt::IBeamCursor);
      break;
    case kCursorCell:
      setCursor(Qt::UpArrowCursor);
      break;
    case kCursorContextMenu:
      setCursor(Qt::ArrowCursor);
      break;
    case kCursorAlias:
      setCursor(Qt::PointingHandCursor);
      break;
    case kCursorProgress:
      setCursor(Qt::WaitCursor);
      break;
    case kCursorNoDrop:
      setCursor(Qt::ForbiddenCursor);
      break;
    case kCursorCopy:
      setCursor(Qt::DragCopyCursor);
      break;
    case kCursorNone:
      setCursor(Qt::BlankCursor);
      break;
    case kCursorNotAllowed:
      setCursor(Qt::ForbiddenCursor);
      break;
    case kCursorZoomIn:
    case kCursorZoomOut:
      setCursor(Qt::SizeAllCursor);
      break;
    case kCursorGrab:
      setCursor(Qt::OpenHandCursor);
      break;
    case kCursorGrabbing:
      setCursor(Qt::ClosedHandCursor);
      break;
    default:
      setCursor(Qt::ArrowCursor);
      break;
  }
}

bool BrowserWidget::nativeEvent(const QByteArray& event_type, void* message,
                                long* result) {
  (void)event_type;
  MSG* windows_message = static_cast<MSG*>(message);
  if (!windows_message) {
    return QWidget::nativeEvent(event_type, message, result);
  }

  switch (windows_message->message) {
    case WM_SYSCHAR:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
      if (browser_service_) {
        browser_service_->SendWindowsKeyEvent(
            static_cast<uint32_t>(windows_message->message),
            static_cast<uintptr_t>(windows_message->wParam),
            static_cast<intptr_t>(windows_message->lParam));
        if (result) {
          *result = 0;
        }
        return true;
      }
      break;
    default:
      break;
  }

  return QWidget::nativeEvent(event_type, message, result);
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

void BrowserWidget::mousePressEvent(QMouseEvent* event) {
  if (browser_service_) {
    browser_service_->SendMouseClickEvent(
        event->pos().x(), event->pos().y(),
        static_cast<int>(event->button()),
        static_cast<int>(event->buttons()),
        false, 1, static_cast<int>(event->modifiers()));
  }
}

void BrowserWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (browser_service_) {
    browser_service_->SendMouseClickEvent(
        event->pos().x(), event->pos().y(),
        static_cast<int>(event->button()),
        static_cast<int>(event->buttons()),
        true, 1, static_cast<int>(event->modifiers()));
  }
}

void BrowserWidget::mouseMoveEvent(QMouseEvent* event) {
  last_mouse_pos_ = event->pos();
  if (browser_service_) {
    browser_service_->SendMouseMoveEvent(
        event->pos().x(), event->pos().y(),
        static_cast<int>(event->buttons()),
        static_cast<int>(event->modifiers()), false);
  }
}

void BrowserWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  if (browser_service_) {
    browser_service_->SendMouseClickEvent(
        event->pos().x(), event->pos().y(),
        static_cast<int>(event->button()),
        static_cast<int>(event->buttons()),
        false, 2, static_cast<int>(event->modifiers()));
  }
}

void BrowserWidget::wheelEvent(QWheelEvent* event) {
  if (!browser_service_) return;

  int delta_x = 0;
  int delta_y = 0;

  QPoint pixel_delta = event->pixelDelta();
  if (!pixel_delta.isNull()) {
    delta_x = pixel_delta.x();
    delta_y = pixel_delta.y();
  } else {
    QPoint angle_delta = event->angleDelta();
    delta_x = angle_delta.x();
    delta_y = angle_delta.y();
  }

  browser_service_->SendMouseWheelEvent(
      event->pos().x(), event->pos().y(),
      static_cast<int>(event->buttons()),
      static_cast<int>(event->modifiers()), delta_x, delta_y);
}

void BrowserWidget::leaveEvent(QEvent* event) {
  QWidget::leaveEvent(event);
  if (browser_service_) {
    browser_service_->SendMouseMoveEvent(
        last_mouse_pos_.x(), last_mouse_pos_.y(),
        0, 0, true);
  }
}

void BrowserWidget::keyPressEvent(QKeyEvent* event) {
  if (!browser_service_) {
    QWidget::keyPressEvent(event);
    return;
  }

  const int virtual_key = static_cast<int>(event->nativeVirtualKey());
  const int scan_code = static_cast<int>(event->nativeScanCode());
  const int modifiers = static_cast<int>(event->modifiers());
  const bool is_keypad = (event->modifiers() & Qt::KeypadModifier) != 0;
  const uint32_t native_key_code = BuildWindowsNativeKeyCode(
      virtual_key, scan_code, event->isAutoRepeat(), false);
  const bool is_tab = (event->key() == Qt::Key_Tab ||
                       event->key() == Qt::Key_Backtab);
  const QByteArray text_utf8 = event->text().toUtf8();
  const std::string text_for_log(text_utf8.constData(),
                                 static_cast<size_t>(text_utf8.size()));

  DiagnosticLog("BrowserWidget::keyPressEvent key=" +
                std::to_string(event->key()) +
                " vk=" + std::to_string(virtual_key) +
                " scan=" + std::to_string(scan_code) +
                " modifiers=" + std::to_string(modifiers) +
                " tab=" + (is_tab ? "true" : "false") +
                " auto=" + (event->isAutoRepeat() ? "true" : "false") +
                " text=[" + text_for_log + "]");

  browser_service_->SendRawKeyDown(virtual_key, native_key_code, modifiers,
                                   is_keypad);
  DiagnosticLog("BrowserWidget::keyPressEvent rawKeyDown done");

  // Char events for printable text (skip Tab/Backtab and auto-repeat)
  if (!is_tab && !event->text().isEmpty() && !event->isAutoRepeat()) {
    DiagnosticLog("BrowserWidget::keyPressEvent entering CHAR path");
    const QString text = event->text();
    for (int index = 0; index < text.size(); ++index) {
      const char16_t ch = static_cast<char16_t>(text.at(index).unicode());
      DiagnosticLog("BrowserWidget::keyPressEvent sending char=" +
                    std::to_string(static_cast<int>(ch)));
      browser_service_->SendCharEvent(native_key_code, modifiers, ch);
    }
    DiagnosticLog("BrowserWidget::keyPressEvent CHAR path done");
  }
  event->accept();
}

void BrowserWidget::keyReleaseEvent(QKeyEvent* event) {
  if (!browser_service_) {
    QWidget::keyReleaseEvent(event);
    return;
  }

  const int virtual_key = static_cast<int>(event->nativeVirtualKey());
  const int scan_code = static_cast<int>(event->nativeScanCode());
  const int modifiers = static_cast<int>(event->modifiers());
  const bool is_keypad = (event->modifiers() & Qt::KeypadModifier) != 0;
  const uint32_t native_key_code =
      BuildWindowsNativeKeyCode(virtual_key, scan_code, true, true);

  browser_service_->SendKeyUp(virtual_key, native_key_code, modifiers,
                              is_keypad);
  event->accept();
}

void BrowserWidget::focusInEvent(QFocusEvent* event) {
  QWidget::focusInEvent(event);
  if (browser_service_) {
    browser_service_->SetBrowserFocus(true);
  }
}

void BrowserWidget::focusOutEvent(QFocusEvent* event) {
  QWidget::focusOutEvent(event);
  if (browser_service_) {
    browser_service_->SetBrowserFocus(false);
  }
}

}  // namespace offscreen
