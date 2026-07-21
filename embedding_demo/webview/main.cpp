#include <QApplication>
#include <QCloseEvent>
#include <QMainWindow>
#include <QTimer>
#include <QUrl>

#include <windows.h>

#include "offscreen_cef/cef_runtime.h"
#include "offscreen_cef/cef_web_view.h"

namespace {

class WebViewDemoWindow final : public QMainWindow {
 public:
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

  void LoadInitialUrl(const QUrl& url) { view_->LoadUrl(url); }

 protected:
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

int main(int argc, char* argv[]) {
  if (const auto exit_code = offscreen::CefRuntime::ExecuteSubprocess(
          ::GetModuleHandleW(nullptr))) {
    return *exit_code;
  }

  QApplication app(argc, argv);
  offscreen::CefRuntime runtime;
  if (!runtime.Initialize()) {
    return 1;
  }

  WebViewDemoWindow window;
  window.show();
  window.LoadInitialUrl(QUrl(QStringLiteral("https://example.com/")));

  const int result = app.exec();
  return runtime.Shutdown() ? result : 1;
}
