#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <QPoint>
#include <QOpenGLWidget>
#include <windows.h>

#include "browser/browser_geometry.h"
#include "browser/browser_paint_geometry.h"
#include "browser/browser_touch_gesture.h"
#include "browser/render_stats.h"
#include "browser/tab_manager.h"
#include "include/cef_drag_data.h"
#include "include/cef_render_handler.h"

class QContextMenuEvent;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QTimer;
class QTouchEvent;

namespace offscreen {

class BrowserFrame;
class BrowserGlRenderer;
class BrowserImeHandler;
class BrowserService;
class GpuCopyPolicy;
class GpuFrameBridge;

// 将 BrowserFrame 中的离屏像素绘制到 Qt 控件，并把 Qt/Windows 输入转发给 CEF。
class BrowserWidget final : public QOpenGLWidget {
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
  void SetGpuFrameBridge(std::shared_ptr<GpuFrameBridge> gpu_frame_bridge);
  /// 绑定 GPU 复制降级策略；paintGL 呈现结果会按资源上报。
  /// @param policy 浏览器服务提供的共享策略。
  void SetGpuCopyPolicy(std::shared_ptr<GpuCopyPolicy> policy);
  /// 绑定渲染性能统计核心；由 1s 定时器周期导出快照。
  /// @param stats 浏览器服务提供的统计核心。
  void SetRenderStats(std::shared_ptr<RenderStats> stats);
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
  /// 接收 CEF 层"页面未拦截右键"通知，延迟一个事件循环后发射 contextMenuRequested。
  /// @param view_x 视图内逻辑 x 坐标（坐标回退源）。
  /// @param view_y 视图内逻辑 y 坐标（坐标回退源）。
  void RequestContextMenu(int view_x, int view_y);

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

 signals:
  /// 用户请求右键菜单时发射。
  /// @param globalPos 菜单出现的全局坐标。
  void contextMenuRequested(const QPoint& globalPos);
  /// 周期（每秒）发射渲染性能快照；空闲时不发射。
  /// @param snapshot 渲染统计快照。
  void renderStatsUpdated(const RenderStatsSnapshot& snapshot);

 protected:
  void initializeGL() override;
  void paintGL() override;
  /// 将 Qt 尺寸和设备缩放变化通知浏览器服务。
  /// @param event Qt 尺寸事件。
  void resizeEvent(QResizeEvent* event) override;
  /// 绘制主视图及可见的 CEF 弹出层帧。
  /// @param event Qt 绘制事件。

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
  /// 将右键菜单请求交给宿主绘制。
  /// @param event Qt 右键菜单事件。
  void contextMenuEvent(QContextMenuEvent* event) override;
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
  /// 将 Qt 触摸事件分派到触摸处理函数。
  /// @param event Qt 事件。
  /// @return 触摸事件始终已处理，其他事件交给基类。
  bool event(QEvent* event) override;
  /// 将 Qt 触摸事件和已确认的手势动作转发给 CEF。
  /// @param event Qt 触摸事件。
  void touchEvent(QTouchEvent* event);
  /// 将 Qt 拖拽进入事件转换为 CEF OSR 拖拽进入。
  /// @param event Qt 拖拽进入事件。
  void dragEnterEvent(QDragEnterEvent* event) override;
  /// 将 Qt 拖拽移动事件转换为 CEF OSR 拖拽移动。
  /// @param event Qt 拖拽移动事件。
  void dragMoveEvent(QDragMoveEvent* event) override;
  /// 通知 CEF 拖拽已离开视图。
  /// @param event Qt 拖拽离开事件。
  void dragLeaveEvent(QDragLeaveEvent* event) override;
  /// 将 Qt 投放事件转换为 CEF OSR drop。
  /// @param event Qt 投放事件。
  void dropEvent(QDropEvent* event) override;

 private:
  /// 周期导出渲染统计：写日志并发射 renderStatsUpdated。
  void EmitRenderStats();
  /// 解析 WM_IME_COMPOSITION 并调用 BrowserService 的输入法接口。
  /// @param wParam Win32 消息 wParam。
  /// @param lParam Win32 消息 lParam。
  void HandleImeCompositionMessage(WPARAM wParam, LPARAM lParam);
  /// 从 Qt MIME 数据构造 CEF 拖拽数据。
  /// @param mime_data Qt 拖拽数据。
  /// @return 可传给 CEF 的拖拽数据；不支持时为 nullptr。
  CefRefPtr<CefDragData> CreateCefDragData(const QMimeData* mime_data) const;
  bool StartCefDragging(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefDragData> drag_data,
                        CefRenderHandler::DragOperationsMask allowed_ops,
                        int screen_x,
                        int screen_y);
  void UpdateCefDragCursor(CefRenderHandler::DragOperation operation);
  void SendCefDragEnter(const QPoint& position, int buttons, int modifiers);
  void FinishCefDragging(const QPoint& position,
                         int buttons,
                         int modifiers,
                         bool dropped);
  void CancelCefDragging();
  std::vector<TouchPointSnapshot> TouchSnapshots(
      const QTouchEvent* event) const;
  void CancelTouchSequence();
  void BeginTouchDragging(const QPoint& position);
  void UpdateTouchDragging(const QPoint& position);
  void EndTouchDragging(const QPoint& position);
  void CancelTouchDragging();

  ResizeCallback resize_callback_;
  std::shared_ptr<BrowserFrame> frame_;
  std::shared_ptr<GpuFrameBridge> gpu_frame_bridge_;
  std::shared_ptr<GpuCopyPolicy> gpu_copy_policy_;
  std::shared_ptr<RenderStats> render_stats_;
  std::unique_ptr<BrowserGlRenderer> gl_renderer_;
  uint64_t last_presented_gpu_frame_generation_ = 0;
  QTimer* stats_timer_ = nullptr;
  BrowserService* browser_service_ = nullptr;
  BrowserImeHandler* ime_handler_ = nullptr;
  TabManager::TabId tab_id_ = TabManager::kInvalidTabId;
  bool is_composing_ = false;
  bool drag_active_ = false;
  bool cef_drag_source_active_ = false;
  bool cef_drag_target_active_ = false;
  bool touch_drag_active_ = false;
  bool touch_navigation_sent_ = false;
  int touch_sequence_primary_id_ = -1;
  TouchGestureStateMachine touch_gesture_;
  CefRefPtr<CefDragData> cef_drag_data_;
  CefRefPtr<CefDragData> touch_drag_data_;
  CefRenderHandler::DragOperationsMask cef_drag_allowed_ops_ =
      DRAG_OPERATION_NONE;
  CefRenderHandler::DragOperation cef_drag_current_op_ = DRAG_OPERATION_NONE;
  QPoint last_mouse_pos_;
  QPoint last_context_menu_pos_;
  std::unordered_map<int, TouchPointSnapshot> active_touch_points_;
};

}  // namespace offscreen
