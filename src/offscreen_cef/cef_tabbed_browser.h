#pragma once

#include <QWidget>

class QTabWidget;
class QUrl;

namespace offscreen {

class CefWebView;

/// 轻量级网页标签容器；宿主可保留自身工具栏和导航界面。
class CefTabbedBrowser final : public QWidget {
  Q_OBJECT

 public:
  /// 创建标签容器。
  /// @param parent Qt 父控件。
  explicit CefTabbedBrowser(QWidget* parent = nullptr);
  /// 销毁标签容器。
  ~CefTabbedBrowser() override;

  /// 新建网页标签并开始加载地址。
  /// @param url 新标签加载的绝对 URL。
  /// @param activate 是否立即切换到新标签。
  /// @return 新建的网页控件。
  CefWebView* OpenTab(const QUrl& url, bool activate = true);
  /// 获取当前激活的网页控件。
  /// @return 当前网页；没有标签时为 nullptr。
  CefWebView* CurrentView() const;
  /// 获取标签数量。
  /// @return 当前标签页总数。
  int tabCount() const;

 public slots:
  /// 请求关闭指定索引的网页标签。
  /// @param index 标签索引。
  void CloseTab(int index);
  /// 请求关闭全部网页标签。
  void CloseAllTabs();

 signals:
  /// 当前标签地址变化时发射。
  /// @param url 当前标签地址。
  void currentUrlChanged(const QUrl& url);
  /// 当前标签标题变化时发射。
  /// @param title 当前标签标题。
  void currentTitleChanged(const QString& title);
  /// 全部网页标签关闭时发射。
  void allTabsClosed();

 protected:
  /// 拦截窗口关闭并等待所有 CEF 浏览器关闭。
  /// @param event Qt 关闭事件。
  void closeEvent(QCloseEvent* event) override;

 private:
  /// 移除已收到 browserClosed 信号的标签。
  /// @param view 已关闭的网页控件。
  void OnTabClosed(CefWebView* view);
  /// 发送切换后当前网页的地址和标题。
  /// @param index 新激活标签索引。
  void UpdateCurrentState(int index);

  QTabWidget* tabs_ = nullptr;
  bool close_requested_ = false;
};

}  // namespace offscreen
