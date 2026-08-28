#pragma once

#include <string>

#include "include/cef_client.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_download_handler.h"
#include "include/cef_focus_handler.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_request_handler.h"

namespace offscreen {

/// 将 CEF 生命周期、加载、显示、输入和弹出事件汇集后转发给 BrowserService。
class BrowserClient final : public CefClient,
                            public CefLifeSpanHandler,
                            public CefLoadHandler,
                            public CefDisplayHandler,
                            public CefRequestHandler,
                            public CefKeyboardHandler,
                            public CefFocusHandler,
                            public CefContextMenuHandler,
                            public CefDownloadHandler {
 public:
  /// BrowserClient 的宿主回调接口，隔离 CEF 回调与 Qt 业务逻辑。
  class Delegate {
   public:
    /// 销毁实现对象。
    virtual ~Delegate() = default;
    /// 接收 CEF 浏览器创建完成通知。
    /// @param browser 新创建的浏览器。
    virtual void OnBrowserCreated(CefRefPtr<CefBrowser> browser) = 0;
    /// 接收浏览器进入关闭流程通知。
    /// @param browser 正在关闭的浏览器。
    virtual void OnBrowserClosing(CefRefPtr<CefBrowser> browser) = 0;
    /// 接收浏览器已完全关闭通知。
    /// @param browser 已关闭的浏览器。
    virtual void OnBrowserClosed(CefRefPtr<CefBrowser> browser) = 0;
    /// 接收加载状态变化。
    /// @param is_loading 是否正在加载。
    /// @param can_go_back 是否可后退。
    /// @param can_go_forward 是否可前进。
    virtual void OnLoadStateChanged(bool is_loading,
                                    bool can_go_back,
                                    bool can_go_forward) = 0;
    /// 接收主框架地址变化。
    /// @param url 最新 UTF-8 地址。
    virtual void OnAddressChanged(const std::string& url) = 0;
    /// 接收页面标题变化。
    /// @param title 最新 UTF-8 标题。
    virtual void OnTitleChanged(const std::string& title) = 0;
    /// 接收加载错误。
    /// @param error_code CEF 错误码。
    /// @param failed_url 失败的 UTF-8 地址。
    /// @param error_text CEF 错误说明。
    virtual void OnLoadError(int error_code,
                             const std::string& failed_url,
                             const std::string& error_text) = 0;
    /// 接收开始下载请求。
    /// @param callback 下载继续回调。
    /// @param suggested_name 建议的 UTF-8 文件名。
    virtual void OnDownloadStarted(
        CefRefPtr<CefBeforeDownloadCallback> callback,
        const std::string& suggested_name) = 0;
    /// 接收下载状态变化。
    /// @param state 下载状态：0 开始、1 完成、2 取消。
    /// @param file_name UTF-8 文件名。
    /// @param full_path UTF-8 完整保存路径。
    virtual void OnDownloadStateChanged(int state,
                                        const std::string& file_name,
                                        const std::string& full_path) = 0;
    /// 接收渲染进程异常退出通知。
    virtual void OnRenderProcessTerminated() = 0;
    /// 接收 CEF 光标变化。
    /// @param cursor_type CEF 光标类型。
    /// @param cursor_handle Win32 光标句柄。
    virtual void OnCursorChanged(int cursor_type,
                                 CefCursorHandle cursor_handle) = 0;
    /// 接收焦点移交请求。
    /// @param next 是否向后切换焦点。
    virtual void OnTakeFocusRequest(bool next) = 0;
    /// 接收 CEF 设置焦点请求。
    virtual void OnSetFocusRequest() = 0;
    /// 接收弹出新窗口请求。
    /// @param url 弹出目标的 UTF-8 地址。
    virtual void OnPopupRequest(const std::string& url) = 0;
    /// 接收 CEF 即将显示默认上下文菜单的通知。
    /// 仅当页面未拦截右键（未 preventDefault contextmenu 事件）时回调。
    /// @param view_x 触发点在视图中的逻辑 x 坐标（DIP）。
    /// @param view_y 触发点在视图中的逻辑 y 坐标（DIP）。
    virtual void OnContextMenuRequested(int view_x, int view_y) = 0;
  };

  /// 创建 CEF 客户端。
  /// @param delegate 接收归一化事件的宿主。
  /// @param render_handler 处理离屏绘制的渲染器。
  BrowserClient(Delegate* delegate,
                CefRefPtr<CefRenderHandler> render_handler);

  /// 返回生命周期处理器。
  /// @return 当前客户端引用。
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
  /// 返回加载处理器。
  /// @return 当前客户端引用。
  CefRefPtr<CefLoadHandler> GetLoadHandler() override;
  /// 返回显示处理器。
  /// @return 当前客户端引用。
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;
  /// 返回离屏渲染处理器。
  /// @return 构造时注入的渲染器。
  CefRefPtr<CefRenderHandler> GetRenderHandler() override;
  /// 返回请求处理器。
  /// @return 当前客户端引用。
  CefRefPtr<CefRequestHandler> GetRequestHandler() override;
  /// 返回键盘处理器。
  /// @return 当前客户端引用。
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override;
  /// 返回焦点处理器。
  /// @return 当前客户端引用。
  CefRefPtr<CefFocusHandler> GetFocusHandler() override;
  /// 返回上下文菜单处理器。
  /// @return 当前客户端引用。
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override;
  /// 返回下载处理器。
  /// @return 当前客户端引用。
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override;

  /// 转发浏览器创建完成事件。
  /// @param browser 新创建的浏览器。
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  /// 转发关闭开始事件。
  /// @param browser 正在关闭的浏览器。
  /// @return false，允许 CEF 继续关闭。
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  /// 转发浏览器完全关闭事件。
  /// @param browser 已关闭的浏览器。
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  /// 转发加载状态。
  /// @param browser 发生变化的浏览器。
  /// @param isLoading 是否正在加载。
  /// @param canGoBack 是否可后退。
  /// @param canGoForward 是否可前进。
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                            bool isLoading,
                            bool canGoBack,
                            bool canGoForward) override;
  /// 转发主框架加载错误，忽略替换导航导致的取消错误。
  /// @param browser 发生错误的浏览器。
  /// @param frame 发生错误的框架。
  /// @param errorCode CEF 错误码。
  /// @param errorText CEF 错误文本。
  /// @param failedUrl 加载失败的地址。
  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode errorCode,
                   const CefString& errorText,
                   const CefString& failedUrl) override;
  /// 清空 CEF 默认上下文菜单，避免与宿主自绘菜单叠加。
  /// @param browser 触发菜单的浏览器。
  /// @param frame 触发菜单的框架。
  /// @param params 菜单上下文信息。
  /// @param model 默认菜单模型。
  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override;
  /// 记录并开始下载。
  /// @param browser 发起下载的浏览器。
  /// @param download_item 下载项。
  /// @param suggested_name 建议文件名。
  /// @param callback 下载继续回调。
  void OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefDownloadItem> download_item,
                        const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override;
  /// 下载完成或取消时转发状态。
  /// @param browser 发起下载的浏览器。
  /// @param download_item 下载项。
  /// @param callback 下载控制回调。
  void OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefDownloadItem> download_item,
                         CefRefPtr<CefDownloadItemCallback> callback) override;
  /// 转发页面标题变化。
  /// @param browser 发生变化的浏览器。
  /// @param title CEF 标题文本。
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  /// 转发主框架地址变化。
  /// @param browser 发生变化的浏览器。
  /// @param frame 地址变化所在框架。
  /// @param url CEF 地址文本。
  void OnAddressChange(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  /// 记录并转发渲染进程终止。
  /// @param browser 受影响浏览器。
  /// @param status CEF 终止状态。
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status) override;
  /// 转发网页要求的鼠标光标。
  /// @param browser 产生光标变化的浏览器。
  /// @param cursor Win32 光标句柄。
  /// @param type CEF 光标类型。
  /// @param custom_cursor_info 自定义光标信息。
  /// @return false，让 CEF 使用宿主处理结果。
  bool OnCursorChange(CefRefPtr<CefBrowser> browser,
                      CefCursorHandle cursor,
                      cef_cursor_type_t type,
                      const CefCursorInfo& custom_cursor_info) override;
  /// 记录预处理键盘事件，不拦截 CEF 默认处理。
  /// @param browser 接收事件的浏览器。
  /// @param event CEF 键盘事件。
  /// @param os_event 原生事件句柄。
  /// @param is_keyboard_shortcut 输出是否为快捷键。
  /// @return false，继续默认处理。
  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                     const CefKeyEvent& event,
                     CefEventHandle os_event,
                     bool* is_keyboard_shortcut) override;
  /// 记录键盘事件，不拦截 CEF 默认处理。
  /// @param browser 接收事件的浏览器。
  /// @param event CEF 键盘事件。
  /// @param os_event 原生事件句柄。
  /// @return false，继续默认处理。
  bool OnKeyEvent(CefRefPtr<CefBrowser> browser,
                  const CefKeyEvent& event,
                  CefEventHandle os_event) override;
  /// 转发 CEF 焦点移交请求。
  /// @param browser 请求焦点移交的浏览器。
  /// @param next 是否向后切换焦点。
  void OnTakeFocus(CefRefPtr<CefBrowser> browser, bool next) override;
  /// 转发 CEF 设置焦点请求。
  /// @param browser 请求焦点的浏览器。
  /// @param source 焦点来源。
  /// @return false，允许 CEF 获取焦点。
  bool OnSetFocus(CefRefPtr<CefBrowser> browser, FocusSource source) override;
  /// 记录 CEF 已获得焦点。
  /// @param browser 获得焦点的浏览器。
  void OnGotFocus(CefRefPtr<CefBrowser> browser) override;

  /// 拦截弹出窗口并交由宿主创建标签页。
  /// @param browser 发起请求的浏览器。
  /// @param frame 发起请求的框架。
  /// @param target_url 目标地址。
  /// @param target_frame_name 目标框架名。
  /// @param target_disposition 打开方式。
  /// @param user_gesture 是否由用户手势触发。
  /// @param popupFeatures CEF 弹出窗口特征。
  /// @param windowInfo 原生窗口配置。
  /// @param client 输出的弹出客户端。
  /// @param settings 输出的浏览器设置。
  /// @param extra_info 可选附加信息。
  /// @param no_javascript_access 是否禁用脚本访问。
  /// @return true，取消 CEF 原生弹窗。
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     const CefString& target_url,
                     const CefString& target_frame_name,
                     CefLifeSpanHandler::WindowOpenDisposition target_disposition,
                     bool user_gesture,
                     const CefPopupFeatures& popupFeatures,
                     CefWindowInfo& windowInfo,
                     CefRefPtr<CefClient>& client,
                     CefBrowserSettings& settings,
                     CefRefPtr<CefDictionaryValue>& extra_info,
                     bool* no_javascript_access) override;

  /// 拦截标签页打开 URL 请求并交由宿主创建标签页。
  /// @param browser 发起请求的浏览器。
  /// @param frame 发起请求的框架。
  /// @param target_url 目标地址。
  /// @param target_disposition 打开方式。
  /// @param user_gesture 是否由用户手势触发。
  /// @return true，取消 CEF 默认打开方式。
  bool OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        const CefString& target_url,
                        CefRequestHandler::WindowOpenDisposition target_disposition,
                        bool user_gesture) override;

 private:
  Delegate* delegate_ = nullptr;
  CefRefPtr<CefRenderHandler> render_handler_;

  IMPLEMENT_REFCOUNTING(BrowserClient);
};

}  // namespace offscreen
