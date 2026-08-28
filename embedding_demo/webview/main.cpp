#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QMainWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <windows.h>

#include "app/diagnostic_log.h"
#include "browser/osr_render_log.h"
#include "browser/render_stats_log.h"
#include "offscreen_cef/cef_runtime.h"
#include "offscreen_cef/cef_web_view.h"

namespace {

/// 演示 CefWebView 嵌入单个 Qt 主窗口，并等待 CEF 异步关闭。
class WebViewDemoWindow final : public QMainWindow {
 public:
  /// 创建单网页演示窗口并连接标题和关闭信号。
  WebViewDemoWindow() {
    setWindowTitle(QStringLiteral("CEF WebView Embedding Demo"));
    resize(1024, 768);

    view_ = new offscreen::CefWebView(this);
    setCentralWidget(view_);
    connect(view_, &offscreen::CefWebView::downloadRequested, view_,
            [view = view_](offscreen::DownloadRequestId id,
                           const QString& suggested_name, const QUrl&) {
              auto* dialog = new QFileDialog(view);
              dialog->setAcceptMode(QFileDialog::AcceptSave);
              dialog->setFileMode(QFileDialog::AnyFile);
              dialog->setDirectory(QStandardPaths::writableLocation(
                  QStandardPaths::DownloadLocation));
              dialog->selectFile(suggested_name);
              dialog->setAttribute(Qt::WA_DeleteOnClose);
              connect(dialog, &QFileDialog::fileSelected, view,
                      [view, id](const QString& path) {
                        view->AcceptDownload(id, path);
                      });
              connect(dialog, &QDialog::rejected, view,
                      [view, id]() { view->CancelDownload(id); });
              dialog->open();
            });
    view_->SetDownloadDecisionMode(
        offscreen::DownloadDecisionMode::kAskHost);
    connect(view_, &offscreen::CefWebView::titleChanged, this,
            [this](const QString& title) {
              setWindowTitle(title.isEmpty()
                                 ? QStringLiteral("CEF WebView Embedding Demo")
                                 : title);
            });
    connect(view_, &offscreen::CefWebView::browserClosed, this,
            [this]() {
              close_allowed_ = true;
              QTimer::singleShot(0, this, [this]() { close(); });
            });
  }

  /// 加载演示页面。
  /// @param url 要加载的绝对地址。
  void LoadInitialUrl(const QUrl& url) { view_->LoadUrl(url); }

 protected:
  /// 首次关闭时请求 CEF 关闭，收到 browserClosed 后才允许窗口退出。
  /// @param event Qt 关闭事件。
  void closeEvent(QCloseEvent* event) override {
    if (!close_allowed_) {
      view_->CloseBrowser();
      event->ignore();
      return;
    }
    QMainWindow::closeEvent(event);
  }

 private:
  offscreen::CefWebView* view_ = nullptr;
  bool close_allowed_ = false;
};

}  // namespace

/// 启动单网页嵌入演示。
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

  QUrl initial_url(QStringLiteral("https://example.com/"));
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

  WebViewDemoWindow window;
  window.show();
  window.LoadInitialUrl(initial_url);

  const int result = app.exec();
  return runtime.Shutdown() ? result : 1;
}
