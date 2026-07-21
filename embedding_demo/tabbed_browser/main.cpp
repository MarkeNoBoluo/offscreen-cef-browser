#include <QApplication>
#include <QCloseEvent>
#include <QMainWindow>
#include <QTimer>
#include <QUrl>

#include <windows.h>

#include "offscreen_cef/cef_runtime.h"
#include "offscreen_cef/cef_tabbed_browser.h"

namespace {

class TabbedBrowserDemoWindow final : public QMainWindow {
 public:
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

  void OpenInitialTab(const QUrl& url) { browser_->OpenTab(url); }

 protected:
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

  TabbedBrowserDemoWindow window;
  window.show();
  window.OpenInitialTab(QUrl(QStringLiteral("https://example.com/")));

  const int result = app.exec();
  return runtime.Shutdown() ? result : 1;
}
