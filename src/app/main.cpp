#include <algorithm>
#include <limits>

#include <QApplication>
#include <QDir>
#include <QMainWindow>
#include <QObject>
#include <QString>
#include <QTimer>

#include <windows.h>

#include "app/app_config.h"
#include "app/browser_app.h"
#include "include/cef_app.h"

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

  QMainWindow main_window;
  main_window.setWindowTitle(
      QStringLiteral("Offscreen CEF Browser - %1")
          .arg(StringToQString(app_config.initial_url)));
  main_window.resize(1024, 768);
  main_window.show();

  const int result = qt_app.exec();

  CefShutdown();
  return result;
}
