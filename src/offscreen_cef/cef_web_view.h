#pragma once

#include <memory>

#include <QUrl>
#include <QWidget>

#include <windows.h>

namespace offscreen {

class BrowserImeHandler;
class BrowserService;
class BrowserWidget;

// 不包含地址栏等浏览器外壳的页面控件，可嵌入任意 Qt 布局、堆叠页或宿主标签页。
class CefWebView final : public QWidget {
  Q_OBJECT

 public:
  /// 创建可嵌入布局的无浏览器外壳页面控件。
  /// @param parent Qt 父控件。
  explicit CefWebView(QWidget* parent = nullptr);
  /// 释放页面控件；若浏览器仍登记则从运行时注销。
  ~CefWebView() override;

  /// 返回当前或待加载的页面地址。
  /// @return Qt URL 对象。
  QUrl url() const;
  /// 返回 CEF 最近报告的页面标题。
  /// @return UTF-8 转换后的 Qt 标题。
  QString title() const;
  /// 查询 CEF 浏览器是否已创建且仍打开。
  /// @return 浏览器打开时为 true。
  bool IsBrowserOpen() const;

 public slots:
  /// 加载绝对 URL；未创建浏览器时会延迟创建。
  /// @param url 要加载的有效绝对地址。
  void LoadUrl(const QUrl& url);
  /// 重新加载当前页面。
  void Reload();
  /// 停止当前导航请求。
  void Stop();
  /// 异步请求关闭 CEF 浏览器。
  void CloseBrowser();

 signals:
  /// 页面地址发生变化时发射。
  /// @param url 最新地址。
  void urlChanged(const QUrl& url);
  /// 页面标题发生变化时发射。
  /// @param title 最新标题。
  void titleChanged(const QString& title);
  /// 页面结束加载时发射。
  /// @param ok 加载没有记录错误时为 true。
  void loadFinished(bool ok);
  /// 页面加载或创建失败时发射。
  /// @param error 错误说明。
  void loadFailed(const QString& error);
  /// 页面请求弹出新窗口时发射。
  /// @param url 新窗口目标地址。
  void newWindowRequested(const QUrl& url);
  /// CEF 浏览器完全关闭时发射。
  void browserClosed();

 protected:
  /// 控件显示后尝试创建待加载的浏览器。
  /// @param event Qt 显示事件。
  void showEvent(QShowEvent* event) override;
  /// 拦截关闭事件，等待 CEF 完成异步关闭。
  /// @param event Qt 关闭事件。
  void closeEvent(QCloseEvent* event) override;

 private:
  friend class CefRuntime;

  /// 在控件可见且运行时就绪时排队创建浏览器。
  void StartBrowserIfReady();
  /// 统一清理关闭后的网页、输入法与运行时登记状态。
  void OnBrowserClosed();
  /// 将已筛选的 Win32 输入法消息交给内部 BrowserWidget。
  /// @param message 原生消息结构。
  /// @param result 输出消息处理结果。
  /// @return 消息被处理时为 true。
  bool HandleNativeImeMessage(MSG* message, long* result);

  std::unique_ptr<BrowserService> browser_service_;
  std::unique_ptr<BrowserImeHandler> ime_handler_;
  BrowserWidget* browser_widget_ = nullptr;
  // 控件显示且 CefRuntime 就绪后，才使用该地址异步创建 CEF 浏览器。
  QUrl pending_url_;
  bool browser_create_requested_ = false;
  bool browser_registered_ = false;
  bool close_requested_ = false;
  // CEF 关闭路径可能从多个分支抵达，确保 browserClosed 只发射一次。
  bool close_notified_ = false;
};

}  // namespace offscreen
