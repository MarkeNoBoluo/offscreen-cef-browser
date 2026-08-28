#pragma once

#include <functional>
#include <cstdint>
#include <memory>
#include <string>

#include <windows.h>

#include "browser/browser_client.h"
#include "browser/browser_close_state.h"
#include "browser/browser_geometry.h"
#include "browser/browser_paint_geometry.h"
#include "browser/download_request_registry.h"
#include "browser/osr_render_handler.h"
#include "include/cef_browser.h"
#include "include/cef_download_handler.h"
#include "include/cef_drag_data.h"

namespace offscreen {

class BrowserFrame;
class GpuCopyPolicy;
class GpuFrameBridge;
class RenderStats;

// 单个 CEF 离屏浏览器的协调者：负责创建、关闭，以及在 Qt 与 CEF
// 之间转发输入、页面状态和渲染通知。
class BrowserService final : public BrowserClient::Delegate {
 public:
  using BrowserClosedCallback = std::function<void()>;
  using CursorChangeCallback =
      std::function<void(int cursor_type, CefCursorHandle cursor_handle)>;
  using ImeCompositionRangeChangedCallback =
      std::function<void(const CefRange&, const std::vector<CefRect>&)>;
  using PopupRequestCallback = std::function<void(const std::string& url)>;
  using AddressChangeCallback = std::function<void(const std::string& url)>;
  using TitleChangeCallback = std::function<void(const std::string& title)>;
  using LoadStateChangeCallback =
      std::function<void(bool is_loading, bool can_go_back, bool can_go_forward)>;
  using LoadErrorCallback =
      std::function<void(int error_code, const std::string& failed_url,
                         const std::string& error_text)>;
  using DownloadRequestCallback =
      std::function<void(DownloadRequestId id,
                         const std::string& suggested_name,
                         const std::string& source_url)>;
  using DownloadStateChangeCallback =
      std::function<void(int state, const std::string& file_name,
                         const std::string& full_path)>;
  using ContextMenuRequestedCallback =
      std::function<void(int view_x, int view_y)>;
  using StartDraggingCallback = OsrRenderHandler::StartDraggingCallback;
  using UpdateDragCursorCallback = OsrRenderHandler::UpdateDragCursorCallback;

  /// 创建浏览器服务及首个帧缓存。
  BrowserService();
  /// 销毁服务持有的 CEF 引用。
  ~BrowserService() override;

  /// 使用窗口句柄创建一个无窗口 CEF 浏览器。
  /// @param parent_handle CEF 用于关联输入和屏幕信息的宿主 HWND。
  /// @param initial_view_rect 初始逻辑视图矩形。
  /// @param initial_device_scale_factor 初始设备缩放系数。
  /// @param initial_url 首次加载的 UTF-8 地址。
  /// @return CEF 是否接受创建请求。
  bool CreateBrowser(HWND parent_handle,
                     BrowserViewRect initial_view_rect,
                     double initial_device_scale_factor,
                     const std::string& initial_url);
  /// 设置 CEF 浏览器真正关闭后的通知回调。
  /// @param browser_closed_callback 关闭完成回调。
  void SetBrowserClosedCallback(BrowserClosedCallback browser_closed_callback);
  /// 设置离屏帧脏区域通知回调。
  /// @param callback 接收待重绘逻辑矩形的回调。
  void SetPaintUpdateCallback(PaintUpdateCallback callback);
  /// 设置 CEF 光标变更通知回调。
  /// @param callback 接收光标类型和 Win32 光标句柄的回调。
  void SetCursorChangeCallback(CursorChangeCallback callback);
  void SetStartDraggingCallback(StartDraggingCallback callback);
  void SetUpdateDragCursorCallback(UpdateDragCursorCallback callback);
  /// 设置 CEF 组合文本范围变更通知回调。
  /// @param callback 接收选择范围和字符矩形的回调。
  void SetImeCompositionRangeChangedCallback(
      ImeCompositionRangeChangedCallback callback);
  /// 设置弹出新窗口请求回调。
  /// @param callback 接收目标 UTF-8 地址的回调。
  void SetPopupRequestCallback(PopupRequestCallback callback);
  /// 设置地址变化回调。
  /// @param callback 接收最新 UTF-8 地址的回调。
  void SetAddressChangeCallback(AddressChangeCallback callback);
  /// 设置标题变化回调。
  /// @param callback 接收最新 UTF-8 标题的回调。
  void SetTitleChangeCallback(TitleChangeCallback callback);
  /// 设置加载状态变化回调。
  /// @param callback 接收加载和前进后退可用状态的回调。
  void SetLoadStateChangeCallback(LoadStateChangeCallback callback);
  /// 设置页面加载失败回调。
  /// @param callback 接收错误码、失败地址和错误文本的回调。
  void SetLoadErrorCallback(LoadErrorCallback callback);
  /// 设置下载保存目录；空路径使用 CEF 默认下载目录。
  /// @param path 宽字符格式的目录路径。
  void SetDownloadDirectory(const std::wstring& path);
  /// 设置下载状态变化回调。
  /// @param callback 接收状态、文件名和完整路径的回调。
  void SetDownloadStateChangeCallback(DownloadStateChangeCallback callback);
  /// 设置下载决策模式；离开宿主确认模式时取消全部待决请求。
  void SetDownloadDecisionMode(DownloadDecisionMode mode);
  /// 设置宿主下载请求通知回调。
  void SetDownloadRequestCallback(DownloadRequestCallback callback);
  /// 接受待决下载；空路径等同取消。
  bool AcceptDownload(DownloadRequestId id, const std::wstring& full_path);
  /// 取消待决下载。
  bool CancelDownload(DownloadRequestId id);
  /// 设置页面未拦截右键时的菜单请求回调。
  /// @param callback 接收触发点视图内逻辑坐标的回调。
  void SetContextMenuRequestedCallback(
      ContextMenuRequestedCallback callback);
  /// 更新渲染处理器的尺寸并通知 CEF 重排。
  /// @param view_rect 最新逻辑视图矩形。
  /// @param device_scale_factor 最新设备缩放系数。
  void Resize(BrowserViewRect view_rect, double device_scale_factor);
  /// 导航到指定地址。
  /// @param url 要加载的 UTF-8 地址。
  void Navigate(const std::string& url);
  /// 重新加载当前页面。
  void Reload();
  /// 停止当前页面加载。
  void Stop();
  /// 后退到历史上一页。
  void GoBack();
  /// 前进到历史下一页。
  void GoForward();
  /// 复制选中文本。
  void Copy();
  /// 剪切选中文本。
  void Cut();
  /// 粘贴剪贴板文本。
  void Paste();
  /// 全选页面文本。
  void SelectAll();
  /// 请求 CEF 关闭浏览器，必要时等待创建或关闭回调。
  /// @return 可立即由 Qt 关闭时为 true。
  bool TryCloseBrowser();

  /// 查询 CEF 浏览器是否已经创建。
  /// @return 浏览器对象存在时为 true。
  bool has_browser() const;
  /// 查询是否已进入关闭流程。
  /// @return 正在关闭时为 true。
  bool is_closing() const;
  /// 查询历史中是否有上一页。
  /// @return 可后退时为 true。
  bool can_go_back() const;
  /// 查询历史中是否有下一页。
  /// @return 可前进时为 true。
  bool can_go_forward() const;
  /// 查询是否正在加载。
  /// @return 正在加载时为 true。
  bool is_loading() const;
  /// 获取最近页面标题。
  /// @return UTF-8 标题。
  std::string title() const;
  /// 获取最近页面地址。
  /// @return UTF-8 地址。
  std::string address() const;
  /// 获取最近加载错误。
  /// @return 空字符串表示没有已记录错误。
  std::string last_error() const;
  /// 获取供 Qt 绘制使用的共享帧缓存。
  /// @return 浏览器帧缓存。
  std::shared_ptr<BrowserFrame> frame() const;
  std::shared_ptr<GpuFrameBridge> gpu_frame_bridge() const;
  /// 返回 GPU 复制降级策略（与 bridge/widget 共享同一实例）。
  /// @return 共享的降级策略。
  std::shared_ptr<GpuCopyPolicy> gpu_copy_policy() const;
  /// 获取渲染性能统计实例。
  /// @return 渲染统计核心；与浏览器服务同生命周期。
  std::shared_ptr<RenderStats> render_stats() const;
  /// 开启或关闭渲染性能统计。
  /// @param enabled 是否启用采集与窗口聚合。
  void SetRenderStatsEnabled(bool enabled);
  /// 查询渲染统计是否启用。
  /// @return 已启用时为 true。
  bool render_stats_enabled() const;

  // 键盘按下、文本输入、抬起必须分别转为 CEF 事件，避免中文输入法或
  // AltGr 等场景被当作普通按键而丢失字符。
  /// 转发原始按键按下事件。
  /// @param windows_key_code Win32 虚拟键码。
  /// @param native_key_code Win32 原生键码。
  /// @param qt_modifiers Qt 修饰键位掩码。
  /// @param is_keypad 是否来自数字键盘。
  void SendRawKeyDown(int windows_key_code, uint32_t native_key_code,
                      int qt_modifiers, bool is_keypad);
  /// 转发可打印字符事件。
  /// @param native_key_code Win32 原生键码。
  /// @param qt_modifiers Qt 修饰键位掩码。
  /// @param character 要提交的 UTF-16 字符。
  void SendCharEvent(uint32_t native_key_code,
                     int qt_modifiers, char16_t character);
  /// 转发按键抬起事件。
  /// @param windows_key_code Win32 虚拟键码。
  /// @param native_key_code Win32 原生键码。
  /// @param qt_modifiers Qt 修饰键位掩码。
  /// @param is_keypad 是否来自数字键盘。
  void SendKeyUp(int windows_key_code, uint32_t native_key_code,
                 int qt_modifiers, bool is_keypad);
  /// 直接将 Win32 键盘消息转换为 CEF 键盘事件。
  /// @param message WM_KEY* 或 WM_SYSKEY* 消息编号。
  /// @param w_param Win32 消息 wParam。
  /// @param l_param Win32 消息 lParam。
  void SendWindowsKeyEvent(uint32_t message, uintptr_t w_param,
                           intptr_t l_param);

  // Qt 坐标和按键状态经映射后直接交给 CEF 的无窗口宿主。
  /// 转发鼠标按下或抬起事件。
  /// @param x 逻辑 x 坐标。
  /// @param y 逻辑 y 坐标。
  /// @param qt_button 本次变化的 Qt 鼠标键。
  /// @param qt_buttons 当前全部按下鼠标键。
  /// @param mouse_up 是否为抬起事件。
  /// @param click_count 连击次数。
  /// @param qt_modifiers Qt 修饰键位掩码。
  void SendMouseClickEvent(int x, int y, int qt_button, int qt_buttons,
                           bool mouse_up, int click_count, int qt_modifiers);
  /// 转发鼠标移动或离开事件。
  /// @param x 逻辑 x 坐标。
  /// @param y 逻辑 y 坐标。
  /// @param qt_buttons 当前全部按下鼠标键。
  /// @param qt_modifiers Qt 修饰键位掩码。
  /// @param mouse_leave 是否为离开视图事件。
  void SendMouseMoveEvent(int x, int y, int qt_buttons,
                           int qt_modifiers, bool mouse_leave);
  /// 转发鼠标滚轮事件。
  /// @param x 逻辑 x 坐标。
  /// @param y 逻辑 y 坐标。
  /// @param qt_buttons 当前全部按下鼠标键。
  /// @param qt_modifiers Qt 修饰键位掩码。
  /// @param delta_x 水平滚动量。
  /// @param delta_y 垂直滚动量。
  void SendMouseWheelEvent(int x, int y, int qt_buttons,
                            int qt_modifiers, int delta_x, int delta_y);
  /// 转发单个触点事件。
  /// @param id 触点标识。
  /// @param x 逻辑 x 坐标。
  /// @param y 逻辑 y 坐标。
  /// @param touch_type CEF_TET_* 触点状态。
  /// @param qt_modifiers Qt 修饰键位掩码。
  /// @param radius_x 触点椭圆半径 x。
  /// @param radius_y 触点椭圆半径 y。
  /// @param pressure 触点压力。
  void SendTouchEvent(int id, float x, float y, int touch_type,
                      int qt_modifiers, float radius_x, float radius_y,
                      float pressure);
  /// 通知 CEF 有外部拖拽数据进入离屏视图。
  /// @param drag_data CEF 拖拽数据。
  /// @param x 逻辑 x 坐标。
  /// @param y 逻辑 y 坐标。
  /// @param qt_buttons 当前全部按下鼠标键。
  /// @param qt_modifiers Qt 修饰键位掩码。
  void SendDragTargetDragEnter(CefRefPtr<CefDragData> drag_data,
                               int x, int y, int qt_buttons,
                               int qt_modifiers,
                               CefBrowserHost::DragOperationsMask allowed_ops =
                                   DRAG_OPERATION_COPY);
  /// 通知 CEF 拖拽数据在离屏视图内移动。
  /// @param x 逻辑 x 坐标。
  /// @param y 逻辑 y 坐标。
  /// @param qt_buttons 当前全部按下鼠标键。
  /// @param qt_modifiers Qt 修饰键位掩码。
  void SendDragTargetDragOver(int x, int y, int qt_buttons,
                              int qt_modifiers,
                              CefBrowserHost::DragOperationsMask allowed_ops =
                                  DRAG_OPERATION_COPY);
  /// 通知 CEF 拖拽数据离开离屏视图。
  void SendDragTargetDragLeave();
  /// 通知 CEF 拖拽数据投放到离屏视图。
  /// @param x 逻辑 x 坐标。
  /// @param y 逻辑 y 坐标。
  /// @param qt_buttons 当前全部按下鼠标键。
  /// @param qt_modifiers Qt 修饰键位掩码。
  void SendDragTargetDrop(int x, int y, int qt_buttons, int qt_modifiers);
  void SendDragSourceEndedAt(int x,
                             int y,
                             CefBrowserHost::DragOperationsMask op);
  void SendDragSourceSystemDragEnded();

  // 焦点和鼠标捕获状态需要同步给 CEF，保证网页输入状态正确。
  /// 同步 Qt 焦点状态到 CEF。
  /// @param focus 是否获得焦点。
  void SetBrowserFocus(bool focus);
  /// 通知 CEF 已失去鼠标捕获。
  void SendCaptureLost();

  // 输入法预编辑、提交和取消由宿主窗口处理，再转发给网页。
  /// 向 CEF 更新输入法预编辑文本与下划线。
  /// @param text 当前预编辑文本。
  /// @param underlines 各分句的视觉属性。
  /// @param replacement_range 被替换的已有文本范围。
  /// @param selection_range 预编辑文本内的选择范围。
  void ImeSetComposition(const CefString& text,
                         const std::vector<CefCompositionUnderline>& underlines,
                         const CefRange& replacement_range,
                         const CefRange& selection_range);
  /// 向 CEF 提交输入法确认文本。
  /// @param text 已确认的文本。
  /// @param replacement_range 被替换的已有文本范围。
  /// @param relative_cursor_pos 提交后相对文本末尾的光标位置。
  void ImeCommitText(const CefString& text,
                     const CefRange& replacement_range,
                     int relative_cursor_pos);
  /// 取消当前 CEF 输入法预编辑状态。
  void ImeCancelComposition();
  /// 结束当前预编辑状态。
  /// @param keep_selection 是否保留当前文本选择。
  void ImeFinishComposingText(bool keep_selection);

  // BrowserClient 回调在此收敛为宿主可订阅的状态与通知。
  /// 记录 CEF 已创建的浏览器，并处理创建期间的关闭请求。
  /// @param browser 新创建的 CEF 浏览器。
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override;
  /// 标记 CEF 已进入关闭阶段。
  /// @param browser 正在关闭的浏览器。
  void OnBrowserClosing(CefRefPtr<CefBrowser> browser) override;
  /// 清理 CEF 引用并通知宿主可以释放 Qt 控件。
  /// @param browser 已关闭的浏览器。
  void OnBrowserClosed(CefRefPtr<CefBrowser> browser) override;
  /// 缓存并转发页面加载状态。
  /// @param is_loading 是否正在加载。
  /// @param can_go_back 是否可后退。
  /// @param can_go_forward 是否可前进。
  void OnLoadStateChanged(bool is_loading,
                          bool can_go_back,
                          bool can_go_forward) override;
  /// 缓存并转发页面地址。
  /// @param url 最新 UTF-8 地址。
  void OnAddressChanged(const std::string& url) override;
  /// 缓存并转发页面标题。
  /// @param title 最新 UTF-8 标题。
  void OnTitleChanged(const std::string& title) override;
  /// 缓存并转发加载错误。
  /// @param error_code CEF 错误码。
  /// @param failed_url 失败的 UTF-8 地址。
  /// @param error_text CEF 生成的错误说明。
  void OnLoadError(int error_code, const std::string& failed_url,
                   const std::string& error_text) override;
  /// 记录渲染进程异常结束。
  void OnRenderProcessTerminated() override;
  /// 将 CEF 光标变更转发给 Qt 控件。
  /// @param cursor_type CEF 光标类型。
  /// @param cursor_handle 对应 Win32 光标句柄。
  void OnCursorChanged(int cursor_type, CefCursorHandle cursor_handle) override;
  /// 接收 CEF 请求把焦点交给相邻控件的通知。
  /// @param next true 表示向后切换焦点。
  void OnTakeFocusRequest(bool next) override;
  /// 接收 CEF 设置焦点请求。
  void OnSetFocusRequest() override;
  /// 将 CEF 弹出窗口请求交给宿主创建新标签。
  /// @param url 弹出目标的 UTF-8 地址。
  void OnPopupRequest(const std::string& url) override;
  /// 根据当前决策模式立即继续或暂存 CEF 下载回调。
  void OnDownloadRequested(
      DownloadRequestId id,
      CefRefPtr<CefBeforeDownloadCallback> callback,
      const std::string& suggested_name,
      const std::string& source_url) override;
  /// 转发下载状态变化。
  /// @param state 下载状态：0 开始、1 完成、2 取消。
  /// @param file_name UTF-8 文件名。
  /// @param full_path UTF-8 完整保存路径。
  void OnDownloadStateChanged(int state, const std::string& file_name,
                              const std::string& full_path) override;
  /// 接收 CEF 即将显示默认上下文菜单的通知并转发给宿主。
  /// @param view_x 触发点在视图中的逻辑 x 坐标。
  /// @param view_y 触发点在视图中的逻辑 y 坐标。
  void OnContextMenuRequested(int view_x, int view_y) override;

 private:
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<OsrRenderHandler> render_handler_;
  CefRefPtr<BrowserClient> client_;
  std::shared_ptr<BrowserFrame> frame_;
  std::shared_ptr<GpuFrameBridge> gpu_frame_bridge_;
  std::shared_ptr<GpuCopyPolicy> gpu_copy_policy_;
  std::shared_ptr<RenderStats> render_stats_;
  PaintUpdateCallback paint_update_callback_;
  CursorChangeCallback cursor_change_callback_;
  ImeCompositionRangeChangedCallback ime_composition_range_changed_callback_;
  StartDraggingCallback start_dragging_callback_;
  UpdateDragCursorCallback update_drag_cursor_callback_;
  bool close_when_created_ = false;
  bool is_closing_ = false;
  bool is_loading_ = false;
  bool can_go_back_ = false;
  bool can_go_forward_ = false;
  BrowserClosedCallback browser_closed_callback_;
  PopupRequestCallback popup_request_callback_;
  AddressChangeCallback address_change_callback_;
  TitleChangeCallback title_change_callback_;
  LoadStateChangeCallback load_state_change_callback_;
  LoadErrorCallback load_error_callback_;
  DownloadStateChangeCallback download_state_change_callback_;
  DownloadDecisionMode download_decision_mode_ =
      DownloadDecisionMode::kAutomatic;
  DownloadRequestCallback download_request_callback_;
  DownloadRequestRegistry pending_downloads_;
  ContextMenuRequestedCallback context_menu_requested_callback_;
  std::wstring download_dir_;
  std::string address_;
  std::string title_;
  std::string last_error_;
};

}  // namespace offscreen
