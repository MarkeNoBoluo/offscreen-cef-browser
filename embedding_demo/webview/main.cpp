#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QFileInfo>
#include <QMainWindow>
#include <QTimer>
#include <QUrl>

#include <windows.h>

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
  if (const auto exit_code = offscreen::CefRuntime::ExecuteSubprocess(
          ::GetModuleHandleW(nullptr))) {
    return *exit_code;
  }

  QApplication app(argc, argv);
  QUrl initial_url(QStringLiteral("http://192.168.42.116"));
  if (app.arguments().size() > 1) {
    const QFileInfo html_file(app.arguments().at(1));
    const QString suffix = html_file.suffix();
    if (!html_file.exists() || !html_file.isFile() ||
        (suffix.compare(QStringLiteral("html"), Qt::CaseInsensitive) != 0 &&
         suffix.compare(QStringLiteral("htm"), Qt::CaseInsensitive) != 0)) {
      qCritical().noquote()
          << QStringLiteral("Local HTML file not found or invalid: %1")
                 .arg(html_file.filePath());
      return 2;
    }
    initial_url = QUrl::fromLocalFile(html_file.absoluteFilePath());
  }

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
