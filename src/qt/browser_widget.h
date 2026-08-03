#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <QWidget>
#include <windows.h>

#include "browser/browser_geometry.h"
#include "browser/browser_paint_geometry.h"
#include "browser/tab_manager.h"
#include "include/cef_render_handler.h"

namespace offscreen {

class BrowserFrame;
class BrowserImeHandler;
class BrowserService;

// 将 BrowserFrame 中的离屏像素绘制到 Qt 控件，并把 Qt/Windows 输入转发给 CEF。
class BrowserWidget final : public QWidget {
  Q_OBJECT
 public:
  using ResizeCallback =
      std::function<void(BrowserViewRect, double device_scale_factor)>;

  /// 创建承载离屏浏览器画面的 Qt 控件。
  /// @param parent Qt 父控件。
  explicit BrowserWidget(QWidget* parent = nullptr);
  /// 销毁控件并停止向已解绑服务转发输入。
  ~BrowserWidget() override;

  /// 获取创建 CEF 无窗口浏览器所需的原生父窗口。
  /// @return Qt 转换出的 HWND。
  HWND NativeParentHandle() const;
  /// 获取当前逻辑视图矩形。
  /// @return 至少为 1x1 的浏览器矩形。
  BrowserViewRect CurrentViewRect() const;
  /// 获取当前屏幕设备缩放系数。
  /// @return 有效的设备像素比。
  double CurrentDeviceScaleFactor() const;
  /// 绑定供 paintEvent 读取的共享帧缓存。
  /// @param frame 浏览器服务提供的帧缓存。
  void SetFrame(std::shared_ptr<BrowserFrame> frame);
  /// 设置尺寸变化通知回调。
  /// @param resize_callback 接收逻辑矩形和缩放系数的回调。
  void SetResizeCallback(ResizeCallback resize_callback);
  /// 根据 CEF 脏区域安排 Qt 重绘。
  /// @param dirty_rects 待刷新的逻辑像素矩形；空列表表示整页刷新。
  void ScheduleFrameUpdate(const std::vector<BrowserViewRect>& dirty_rects);
  /// 绑定接收鼠标、键盘和焦点事件的浏览器服务。
  /// @param service 服务指针；nullptr 表示解绑。
  void SetBrowserService(BrowserService* service);
  /// 绑定处理原生输入法消息的处理器。
  /// @param handler 处理器指针；nullptr 表示解绑。
  void SetImeHandler(BrowserImeHandler* handler);
  /// 接收 CEF 提供的组合文本字符位置。
  /// @param selected_range 当前选择范围。
  /// @param bounds 各字符的 CEF 边界矩形。
  void OnImeCompositionRangeChanged(const CefRange& selected_range,
                                    const std::vector<CefRect>& bounds);
  /// 应用 CEF 要求的鼠标光标。
  /// @param cursor_type CEF 光标类型。
  /// @param cursor_handle 可选的 Win32 自定义光标句柄。
  void SetCefCursor(int cursor_type, HCURSOR cursor_handle);

  /// 处理 TabManager 已筛选的原生 IME 消息。
  /// @param windows_message Win32 MSG 结构。
  /// @param result 输出原生消息处理结果。
  /// @return 消息被输入法处理器消费时为 true。
  bool HandleImeNativeMessage(MSG* windows_message, qintptr* result);

  /// 记录此控件所属的标签标识。
  /// @param id TabManager 分配的标签标识。
  void SetTabId(TabManager::TabId id) { tab_id_ = id; }
  /// 获取所属标签标识。
  /// @return 当前标签标识，无效时为 kInvalidTabId。
  TabManager::TabId tab_id() const { return tab_id_; }

 protected:
  /// 将 Qt 尺寸和设备缩放变化通知浏览器服务。
  /// @param event Qt 尺寸事件。
  void resizeEvent(QResizeEvent* event) override;
  /// 绘制主视图及可见的 CEF 弹出层帧。
  /// @param event Qt 绘制事件。
  void paintEvent(QPaintEvent* event) override;

  // Qt 事件在这里转换成鼠标、滚轮、键盘、焦点等 CEF 输入事件。
  /// 转发鼠标按下事件。
  /// @param event Qt 鼠标事件。
  void mousePressEvent(QMouseEvent* event) override;
  /// 转发鼠标抬起事件。
  /// @param event Qt 鼠标事件。
  void mouseReleaseEvent(QMouseEvent* event) override;
  /// 转发鼠标移动事件。
  /// @param event Qt 鼠标事件。
  void mouseMoveEvent(QMouseEvent* event) override;
  /// 转发双击事件。
  /// @param event Qt 鼠标事件。
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  /// 转发滚轮事件。
  /// @param event Qt 滚轮事件。
  void wheelEvent(QWheelEvent* event) override;
  /// 通知 CEF 鼠标已离开视图。
  /// @param event Qt 离开事件。
  void leaveEvent(QEvent* event) override;
  /// 区分预编辑与普通文本后转发按键按下。
  /// @param event Qt 键盘事件。
  void keyPressEvent(QKeyEvent* event) override;
  /// 转发按键抬起。
  /// @param event Qt 键盘事件。
  void keyReleaseEvent(QKeyEvent* event) override;
  /// 将焦点获得状态同步到 CEF。
  /// @param event Qt 焦点事件。
  void focusInEvent(QFocusEvent* event) override;
  /// 取消预编辑并将失焦状态同步到 CEF。
  /// @param event Qt 焦点事件。
  void focusOutEvent(QFocusEvent* event) override;

 private:
  /// 解析 WM_IME_COMPOSITION 并调用 BrowserService 的输入法接口。
  /// @param wParam Win32 消息 wParam。
  /// @param lParam Win32 消息 lParam。
  void HandleImeCompositionMessage(WPARAM wParam, LPARAM lParam);

  ResizeCallback resize_callback_;
  std::shared_ptr<BrowserFrame> frame_;
  BrowserService* browser_service_ = nullptr;
  BrowserImeHandler* ime_handler_ = nullptr;
  TabManager::TabId tab_id_ = TabManager::kInvalidTabId;
  bool is_composing_ = false;
  QPoint last_mouse_pos_;
};

}  // namespace offscreen
