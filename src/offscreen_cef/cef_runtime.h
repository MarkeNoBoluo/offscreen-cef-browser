#pragma once

#include <memory>
#include <optional>
#include <string>

#include <windows.h>

namespace offscreen {

class CefWebView;

/// 初始化进程级 CEF 运行时所需的本地路径。
struct CefRuntimeOptions {
  /// CEF 子进程可执行文件路径。
  std::wstring subprocess_path;
  /// Chromium 缓存目录路径。
  std::wstring cache_path;
  /// CEF 自身日志文件路径。
  std::wstring log_path;
};

/// 进程级 CEF 运行时。宿主 Qt 进程只能创建一个实例，且必须在全部 CefWebView
/// 关闭后才允许停止。
class CefRuntime final {
 public:
  /// 创建尚未初始化的运行时对象。
  CefRuntime();
  /// 销毁运行时对象；调用方应先完成 Shutdown。
  ~CefRuntime();

  CefRuntime(const CefRuntime&) = delete;
  CefRuntime& operator=(const CefRuntime&) = delete;

  /// 在构造 QApplication 前处理 CEF 子进程入口。
  /// @param instance 当前模块实例句柄。
  /// @return 有值表示当前进程是 CEF 子进程，调用方必须立即返回该退出码。
  static std::optional<int> ExecuteSubprocess(HINSTANCE instance);

  /// 使用给定路径初始化主进程 CEF。
  /// @param options 子进程、缓存和日志路径。
  /// @return 初始化是否成功。
  bool Initialize(const CefRuntimeOptions& options = {});
  /// 在所有已登记网页关闭后停止 CEF。
  /// @return 是否已安全完成停止。
  bool Shutdown();
  /// 查询 CEF 是否已完成初始化。
  /// @return 已初始化时为 true。
  bool IsInitialized() const;
  /// 查询是否不存在仍打开的网页控件。
  /// @return 所有网页均关闭时为 true。
  bool AllBrowsersClosed() const;
  /// 请求关闭所有已登记网页。
  void CloseAllBrowsers();

  /// 返回当前进程唯一的活动运行时。
  /// @return 活动实例，未初始化时为 nullptr。
  static CefRuntime* Active();

 private:
  friend class CefWebView;

  /// 登记已创建的网页，用于约束 Shutdown 时机。
  /// @param view 已成功创建浏览器的网页控件。
  void RegisterWebView(CefWebView* view);
  /// 取消登记已关闭的网页。
  /// @param view 已关闭浏览器的网页控件。
  void UnregisterWebView(CefWebView* view);

  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace offscreen
