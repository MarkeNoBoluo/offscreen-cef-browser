#include <windows.h>

#include "app/browser_app.h"
#include "app/diagnostic_log.h"
#include "include/cef_app.h"

/// CEF 专用子进程入口，仅执行 CEF 进程逻辑而不创建 Qt 窗口。
/// @param argc 进程参数个数。
/// @param argv 进程参数数组。
/// @return CefExecuteProcess 返回的子进程退出码。
int main(int argc, char* argv[]) {
  offscreen::SetDiagnosticLogFileToApplicationDirectory();
  CefMainArgs main_args(::GetModuleHandleW(nullptr));
  CefRefPtr<offscreen::BrowserApp> app(new offscreen::BrowserApp);
  return CefExecuteProcess(main_args, app.get(), nullptr);
}
