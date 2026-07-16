#include "app/browser_app.h"

#include <atomic>
#include <sstream>
#include <utility>

#include "app/diagnostic_log.h"
#include "include/cef_command_line.h"

namespace offscreen {

BrowserApp::BrowserApp(MessagePumpScheduler message_pump_scheduler)
    : message_pump_scheduler_(std::move(message_pump_scheduler)) {}

void BrowserApp::SetMessagePumpScheduler(
    MessagePumpScheduler message_pump_scheduler) {
  message_pump_scheduler_ = std::move(message_pump_scheduler);
}

CefRefPtr<CefBrowserProcessHandler> BrowserApp::GetBrowserProcessHandler() {
  return this;
}

void BrowserApp::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
  DiagnosticLog("BrowserApp::OnBeforeCommandLineProcessing process_type=[" +
                process_type.ToString() + "]");
  if (!process_type.empty()) {
    return;
  }

  command_line->AppendSwitch("use-alloy-style");
  DiagnosticLog("BrowserApp appended switch: use-alloy-style");
}

void BrowserApp::OnScheduleMessagePumpWork(int64_t delay_ms) {
  static std::atomic<int> schedule_count{0};
  if (ShouldDiagnosticLog(schedule_count, 20, 100)) {
    std::ostringstream stream;
    stream << "BrowserApp::OnScheduleMessagePumpWork delay_ms=" << delay_ms;
    DiagnosticLog(stream.str());
  }
  if (message_pump_scheduler_) {
    message_pump_scheduler_(delay_ms);
  }
}

}  // namespace offscreen
