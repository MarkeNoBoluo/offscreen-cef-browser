#include <algorithm>
#include <limits>
#include <memory>

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QMainWindow>
#include <QObject>
#include <QString>
#include <QTimer>

#include <windows.h>

#include "app/app_config.h"
#include "app/browser_app.h"
#include "app/browser_window_title.h"
#include "browser/browser_service.h"
#include "include/cef_app.h"
#include "qt/browser_widget.h"

namespace {

QString NativePath(const QString& path) {
  return QDir::toNativeSeparators(path);
}

QString StringToQString(const std::string& value) {
  return QString::fromUtf8(value.c_str(), static_cast<int>(value.size()));
}

void AssignCefString(cef_string_t* cef_string, const QString& value) {
  cef_string_set(reinterpret_cast<const cef_char_t*>(value.utf16()),
                 static_cast<size_t>(value.size()), cef_string, true);
}

class BrowserMainWindow final : public QMainWindow {
 public:
  explicit BrowserMainWindow(offscreen::BrowserService* browser_service)
      : browser_service_(browser_service) {}

 protected:
  void closeEvent(QCloseEvent* event) override {
    if (browser_service_ && !browser_service_->TryCloseBrowser()) {
      event->ignore();
      return;
    }
    QMainWindow::closeEvent(event);
  }

 private:
  offscreen::BrowserService* browser_service_ = nullptr;
};

}  // namespace

int main(int argc, char* argv[]) {
  CefEnableHighDPISupport();

  CefMainArgs main_args(::GetModuleHandleW(nullptr));

  CefRefPtr<offscreen::BrowserApp> cef_app(new offscreen::BrowserApp());

  const int exit_code = CefExecuteProcess(main_args, cef_app.get(), nullptr);
  if (exit_code >= 0) {
    return exit_code;
  }

  QApplication qt_app(argc, argv);
  QObject message_pump_context;
  cef_app->SetMessagePumpScheduler([&message_pump_context](int64_t delay_ms) {
    const int64_t bounded_delay =
        std::min(std::max<int64_t>(delay_ms, 0),
                 static_cast<int64_t>(std::numeric_limits<int>::max()));
    QTimer::singleShot(static_cast<int>(bounded_delay), &message_pump_context,
                       []() { CefDoMessageLoopWork(); });
  });

  const offscreen::AppConfig app_config =
      offscreen::AppConfig::FromArgs(argc, argv);

  const QString app_dir = QCoreApplication::applicationDirPath();
  const QString subprocess_path =
      NativePath(QDir(app_dir).filePath("offscreen_cef_subprocess.exe"));
  const QString cache_path = NativePath(QDir(app_dir).filePath("cef_cache"));
  const QString log_path = NativePath(QDir(app_dir).filePath("cef.log"));

  CefSettings settings;
  settings.no_sandbox = true;
  settings.external_message_pump = true;
  settings.windowless_rendering_enabled = true;
  AssignCefString(&settings.browser_subprocess_path, subprocess_path);
  AssignCefString(&settings.cache_path, cache_path);
  AssignCefString(&settings.log_file, log_path);

  if (!CefInitialize(main_args, settings, cef_app.get(), nullptr)) {
    return 1;
  }

  offscreen::BrowserService browser_service;
  BrowserMainWindow main_window(&browser_service);
  main_window.setWindowTitle(
      QStringLiteral("Offscreen CEF Browser - %1")
          .arg(StringToQString(app_config.initial_url)));

  auto browser_widget = std::make_unique<offscreen::BrowserWidget>();
  offscreen::BrowserWidget* browser_widget_ptr = browser_widget.get();
  main_window.setCentralWidget(browser_widget.release());

  browser_widget_ptr->SetResizeCallback(
      [&browser_service](offscreen::BrowserViewRect view_rect) {
        browser_service.Resize(view_rect);
      });
  browser_service.SetBrowserClosedCallback([&main_window]() {
    QTimer::singleShot(0, &main_window, [&main_window]() { main_window.close(); });
  });

  main_window.resize(1024, 768);
  main_window.show();

  QTimer::singleShot(0, &main_window, [&browser_service, browser_widget_ptr,
                                       &main_window,
                                       initial_url = app_config.initial_url]() {
    if (!browser_service.CreateBrowser(browser_widget_ptr->NativeParentHandle(),
                                       browser_widget_ptr->CurrentViewRect(),
                                       initial_url)) {
      main_window.setWindowTitle(StringToQString(
          offscreen::BrowserCreationFailedTitle(browser_service.last_error())));
    }
  });

  const int result = qt_app.exec();

  CefShutdown();
  return result;
}
