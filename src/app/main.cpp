#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <sstream>

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QMainWindow>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <windows.h>

#include "app/app_config.h"
#include "app/browser_app.h"
#include "app/browser_window_title.h"
#include "app/diagnostic_log.h"
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
  offscreen::DiagnosticLog("main entered");
  CefEnableHighDPISupport();

  CefMainArgs main_args(::GetModuleHandleW(nullptr));

  CefRefPtr<offscreen::BrowserApp> cef_app(new offscreen::BrowserApp());

  const int exit_code = CefExecuteProcess(main_args, cef_app.get(), nullptr);
  if (exit_code >= 0) {
    offscreen::DiagnosticLog("CefExecuteProcess handled subprocess exit_code=" +
                             std::to_string(exit_code));
    return exit_code;
  }
  offscreen::DiagnosticLog("CefExecuteProcess returned browser-process path");

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
  offscreen::DiagnosticLog("AppConfig initial_url=[" + app_config.initial_url +
                           "]");

  const QString app_dir = QCoreApplication::applicationDirPath();
  const QString subprocess_path =
      NativePath(QDir(app_dir).filePath("offscreen_cef_subprocess.exe"));
  const QString cache_path = NativePath(QDir(app_dir).filePath("cef_cache"));
  const QString log_path = NativePath(QDir(app_dir).filePath("cef.log"));

  CefSettings settings;
  settings.no_sandbox = true;
  settings.external_message_pump = false;
  settings.windowless_rendering_enabled = true;
  AssignCefString(&settings.browser_subprocess_path, subprocess_path);
  AssignCefString(&settings.cache_path, cache_path);
  AssignCefString(&settings.log_file, log_path);

  {
    std::ostringstream stream;
    stream << "CefSettings no_sandbox=" << settings.no_sandbox
           << " external_message_pump=" << settings.external_message_pump
           << " windowless_rendering_enabled="
           << settings.windowless_rendering_enabled
           << " subprocess_path=[" << subprocess_path.toStdString() << "]"
           << " cache_path=[" << cache_path.toStdString() << "]"
           << " log_path=[" << log_path.toStdString() << "]";
    offscreen::DiagnosticLog(stream.str());
  }

  if (!CefInitialize(main_args, settings, cef_app.get(), nullptr)) {
    offscreen::DiagnosticLog("CefInitialize failed");
    return 1;
  }
  offscreen::DiagnosticLog("CefInitialize succeeded");

  QTimer cef_work_timer;
  cef_work_timer.setInterval(10);
  QObject::connect(&cef_work_timer, &QTimer::timeout, []() {
    static std::atomic<int> pump_count{0};
    if (offscreen::ShouldDiagnosticLog(pump_count, 20, 100)) {
      offscreen::DiagnosticLog("Qt timer calling CefDoMessageLoopWork");
    }
    CefDoMessageLoopWork();
  });
  cef_work_timer.start();
  offscreen::DiagnosticLog("Started Qt CefDoMessageLoopWork timer interval=10ms");

  offscreen::BrowserService browser_service;
  BrowserMainWindow main_window(&browser_service);
  main_window.setWindowTitle(
      QStringLiteral("Offscreen CEF Browser - %1")
          .arg(StringToQString(app_config.initial_url)));

  auto browser_widget = std::make_unique<offscreen::BrowserWidget>();
  offscreen::BrowserWidget* browser_widget_ptr = browser_widget.get();
  main_window.setCentralWidget(browser_widget.release());

  browser_widget_ptr->SetFrame(browser_service.frame());
  browser_service.SetPaintUpdateCallback(
      [browser_widget_ptr](
          const std::vector<offscreen::BrowserViewRect>& dirty_rects) {
        offscreen::DiagnosticLog("main paint update callback dirty_count=" +
                                 std::to_string(dirty_rects.size()));
        browser_widget_ptr->ScheduleFrameUpdate(dirty_rects);
      });

  browser_widget_ptr->SetResizeCallback(
      [&browser_service](offscreen::BrowserViewRect view_rect,
                         double device_scale_factor) {
        browser_service.Resize(view_rect, device_scale_factor);
      });
  browser_widget_ptr->SetBrowserService(&browser_service);
  QPointer<offscreen::BrowserWidget> browser_widget_guard(browser_widget_ptr);
  browser_service.SetCursorChangeCallback(
      [browser_widget_guard](int cursor_type, HCURSOR cursor_handle) {
        if (!browser_widget_guard) {
          return;
        }
        QMetaObject::invokeMethod(
            browser_widget_guard.data(),
            [browser_widget_guard, cursor_type, cursor_handle]() {
              if (browser_widget_guard) {
                browser_widget_guard->SetCefCursor(cursor_type, cursor_handle);
              }
            },
            Qt::QueuedConnection);
      });
  browser_service.SetBrowserClosedCallback([&main_window]() {
    offscreen::DiagnosticLog("Browser closed callback invoked");
    QTimer::singleShot(0, &main_window, [&main_window]() { main_window.close(); });
  });

  main_window.resize(1024, 768);
  main_window.show();

  QTimer::singleShot(0, &main_window,
                     [&browser_service, browser_widget_ptr, &main_window,
                      initial_url = app_config.initial_url]() {
                       offscreen::DiagnosticLog(
                           "CreateBrowser timer fired hwnd=" +
                           offscreen::HexValue(reinterpret_cast<uintptr_t>(
                               browser_widget_ptr->NativeParentHandle())) +
                           " rect=" +
                           std::to_string(
                               browser_widget_ptr->CurrentViewRect().width) +
                           "x" +
                           std::to_string(
                               browser_widget_ptr->CurrentViewRect().height) +
                           " scale=" +
                           std::to_string(browser_widget_ptr
                                              ->CurrentDeviceScaleFactor()));
                       if (!browser_service.CreateBrowser(
                               browser_widget_ptr->NativeParentHandle(),
                               browser_widget_ptr->CurrentViewRect(),
                               browser_widget_ptr->CurrentDeviceScaleFactor(),
                               initial_url)) {
                         main_window.setWindowTitle(StringToQString(
                             offscreen::BrowserCreationFailedTitle(
                                 browser_service.last_error())));
                       }
                     });

  const int result = qt_app.exec();
  offscreen::DiagnosticLog("Qt event loop exited result=" +
                           std::to_string(result));

  CefShutdown();
  offscreen::DiagnosticLog("CefShutdown completed");
  return result;
}
