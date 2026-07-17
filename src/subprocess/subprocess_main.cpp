#include <windows.h>

#include "app/browser_app.h"
#include "app/diagnostic_log.h"
#include "include/cef_app.h"

int main(int argc, char* argv[]) {
  offscreen::SetDiagnosticLogFileToApplicationDirectory();
  CefMainArgs main_args(::GetModuleHandleW(nullptr));
  CefRefPtr<offscreen::BrowserApp> app(new offscreen::BrowserApp);
  return CefExecuteProcess(main_args, app.get(), nullptr);
}
