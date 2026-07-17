#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <QAbstractNativeEventFilter>
#include <QWidget>
#include <windows.h>

#include "browser/browser_geometry.h"
#include "browser/browser_paint_geometry.h"
#include "include/cef_render_handler.h"

namespace offscreen {

class BrowserFrame;
class BrowserImeHandler;
class BrowserService;

class BrowserWidget final : public QWidget, public QAbstractNativeEventFilter {
 public:
  using ResizeCallback =
      std::function<void(BrowserViewRect, double device_scale_factor)>;

  explicit BrowserWidget(QWidget* parent = nullptr);
  ~BrowserWidget() override;

  HWND NativeParentHandle() const;
  BrowserViewRect CurrentViewRect() const;
  double CurrentDeviceScaleFactor() const;
  void SetFrame(std::shared_ptr<BrowserFrame> frame);
  void SetResizeCallback(ResizeCallback resize_callback);
  void ScheduleFrameUpdate(const std::vector<BrowserViewRect>& dirty_rects);
  void SetBrowserService(BrowserService* service);
  void SetImeHandler(BrowserImeHandler* handler);
  void OnImeCompositionRangeChanged(const CefRange& selected_range,
                                    const std::vector<CefRect>& bounds);
  void SetCefCursor(int cursor_type, HCURSOR cursor_handle);

 protected:
  bool nativeEventFilter(const QByteArray& event_type, void* message,
                         long* result) override;
  void resizeEvent(QResizeEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

  // Input event overrides (V2.3)
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;

 private:
  bool HandleImeNativeMessage(MSG* windows_message, long* result);
  void HandleImeCompositionMessage(WPARAM wParam, LPARAM lParam);

  ResizeCallback resize_callback_;
  std::shared_ptr<BrowserFrame> frame_;
  BrowserService* browser_service_ = nullptr;
  BrowserImeHandler* ime_handler_ = nullptr;
  bool is_composing_ = false;
  QPoint last_mouse_pos_;
};

}  // namespace offscreen
