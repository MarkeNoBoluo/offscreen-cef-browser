#include <QApplication>
#include <QCloseEvent>
#include <QMainWindow>
#include <QTimer>
#include <QUrl>

#include <windows.h>

#include "app/diagnostic_log.h"
#include "browser/osr_render_log.h"
#include "browser/render_stats_log.h"
#include "offscreen_cef/cef_runtime.h"
#include "offscreen_cef/cef_tabbed_browser.h"

namespace {

/// 演示 CefTabbedBrowser 嵌入 Qt 主窗口和多标签异步关闭。
class TabbedBrowserDemoWindow final : public QMainWindow {
 public:
  /// 创建多标签演示窗口并连接标题和全部关闭信号。
  TabbedBrowserDemoWindow() {
    setWindowTitle(QStringLiteral("CEF Tabbed Browser Embedding Demo"));
    resize(1024, 768);

    browser_ = new offscreen::CefTabbedBrowser(this);
    setCentralWidget(browser_);
    connect(browser_, &offscreen::CefTabbedBrowser::currentTitleChanged, this,
            [this](const QString& title) {
              setWindowTitle(title.isEmpty()
                                 ? QStringLiteral("CEF Tabbed Browser Embedding Demo")
                                 : title);
            });
    connect(browser_, &offscreen::CefTabbedBrowser::allTabsClosed, this,
            [this]() {
              close_allowed_ = true;
              QTimer::singleShot(0, this, [this]() { close(); });
            });
  }

  /// 打开首个演示标签。
  /// @param url 要加载的绝对地址。
  void OpenInitialTab(const QUrl& url) { browser_->OpenTab(url); }

 protected:
  /// 首次关闭时请求全部标签关闭，完成后才允许窗口退出。
  /// @param event Qt 关闭事件。
  void closeEvent(QCloseEvent* event) override {
    if (!close_allowed_) {
      browser_->CloseAllTabs();
      event->ignore();
      return;
    }
    QMainWindow::closeEvent(event);
  }

 private:
  offscreen::CefTabbedBrowser* browser_ = nullptr;
  bool close_allowed_ = false;
};

}  // namespace

/// 启动多标签嵌入演示。
/// @param argc 进程参数个数。
/// @param argv 进程参数数组。
/// @return CEF 子进程退出码、Qt 事件循环结果或失败码。
int main(int argc, char* argv[]) {
  offscreen::SetDiagnosticLogFileToApplicationDirectory();
  offscreen::SetOsrRenderLogFileToApplicationDirectory();
  offscreen::SetRenderStatsLogFileToApplicationDirectory();
  if (const auto exit_code = offscreen::CefRuntime::ExecuteSubprocess(
          ::GetModuleHandleW(nullptr))) {
    return *exit_code;
  }

  QApplication app(argc, argv);
  offscreen::CefRuntime runtime;
  if (!runtime.Initialize()) {
    return 1;
  }

  TabbedBrowserDemoWindow window;
  window.show();
  window.OpenInitialTab(QUrl(QStringLiteral("http://192.168.42.116")));

  const int result = app.exec();
  return runtime.Shutdown() ? result : 1;
}
