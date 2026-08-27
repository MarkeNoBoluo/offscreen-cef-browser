#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QMainWindow>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <windows.h>

#include "app/diagnostic_log.h"
#include "browser/osr_render_log.h"
#include "browser/render_stats_log.h"
#include "offscreen_cef/cef_runtime.h"
#include "offscreen_cef/cef_tabbed_browser.h"
#include "offscreen_cef/cef_web_view.h"

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
    connect(browser_, &offscreen::CefTabbedBrowser::downloadRequested, browser_,
            [](offscreen::CefWebView* view, offscreen::DownloadRequestId id,
               const QString& suggested_name, const QUrl&) {
              QPointer<offscreen::CefWebView> guarded_view(view);
              auto* dialog = new QFileDialog(view);
              dialog->setAcceptMode(QFileDialog::AcceptSave);
              dialog->setFileMode(QFileDialog::AnyFile);
              dialog->setDirectory(QStandardPaths::writableLocation(
                  QStandardPaths::DownloadLocation));
              dialog->selectFile(suggested_name);
              dialog->setAttribute(Qt::WA_DeleteOnClose);
              connect(dialog, &QFileDialog::fileSelected, view,
                      [guarded_view, id](const QString& path) {
                        if (guarded_view) {
                          guarded_view->AcceptDownload(id, path);
                        }
                      });
              connect(dialog, &QDialog::rejected, view, [guarded_view, id]() {
                if (guarded_view) {
                  guarded_view->CancelDownload(id);
                }
              });
              dialog->open();
            });
    browser_->SetDownloadDecisionMode(
        offscreen::DownloadDecisionMode::kAskHost);
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

  QUrl initial_url(QStringLiteral("http://192.168.42.116"));
  if (argc > 2) {
    return 2;
  }
  if (argc == 2) {
    const QFileInfo html_file(QString::fromLocal8Bit(argv[1]));
    const QString suffix = html_file.suffix().toLower();
    if (!html_file.exists() || !html_file.isFile() ||
        (suffix != QStringLiteral("html") && suffix != QStringLiteral("htm"))) {
      return 2;
    }
    initial_url = QUrl::fromLocalFile(html_file.absoluteFilePath());
  }

  QApplication app(argc, argv);
  offscreen::CefRuntime runtime;
  if (!runtime.Initialize()) {
    return 1;
  }

  TabbedBrowserDemoWindow window;
  window.show();
  window.OpenInitialTab(initial_url);

  const int result = app.exec();
  return runtime.Shutdown() ? result : 1;
}
