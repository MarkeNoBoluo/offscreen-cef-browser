#pragma once

#include <QPoint>
#include <QWidget>

#include "browser/render_stats.h"

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
  /// 后退当前网页。
  void GoBack();
  /// 前进当前网页。
  void GoForward();
  /// 重新加载当前网页。
  void Reload();
  /// 停止当前网页加载。
  void Stop();
  /// 复制当前网页选中文本。
  void Copy();
  /// 剪切当前网页选中文本。
  void Cut();
  /// 粘贴剪贴板文本到当前网页。
  void Paste();
  /// 全选当前网页文本。
  void SelectAll();

 signals:
  /// 当前标签地址变化时发射。
  /// @param url 当前标签地址。
  void currentUrlChanged(const QUrl& url);
  /// 当前标签标题变化时发射。
  /// @param title 当前标签标题。
  void currentTitleChanged(const QString& title);
  /// 当前网页加载状态变化时发射。
  /// @param isLoading 是否正在加载。
  /// @param canGoBack 是否可后退。
  /// @param canGoForward 是否可前进。
  void currentLoadingStateChanged(bool isLoading, bool canGoBack,
                                  bool canGoForward);
  /// 当前网页加载错误时发射。
  /// @param errorCode CEF 错误码。
  /// @param failedUrl 失败的地址。
  /// @param errorText 错误说明。
  void currentLoadError(int errorCode, const QString& failedUrl,
                        const QString& errorText);
  /// 当前网页下载状态变化时发射。
  /// @param state 下载状态：0 开始、1 完成、2 取消。
  /// @param fileName 文件名。
  /// @param fullPath 完整保存路径。
  void currentDownloadStateChanged(int state, const QString& fileName,
                                   const QString& fullPath);
  /// 当前网页请求右键菜单时发射。
  /// @param globalPos 菜单出现的全局坐标。
  void currentContextMenuRequested(const QPoint& globalPos);
  /// 当前标签渲染性能快照更新时发射。
  /// @param snapshot 当前网页的渲染统计快照。
  void currentRenderStatsUpdated(const RenderStatsSnapshot& snapshot);
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
