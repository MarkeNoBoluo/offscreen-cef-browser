#include "app/browser_app.h"

#include <utility>

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
  if (!process_type.empty()) {
    return;
  }

  command_line->AppendSwitch("use-alloy-style");
}

void BrowserApp::OnScheduleMessagePumpWork(int64_t delay_ms) {
  if (message_pump_scheduler_) {
    message_pump_scheduler_(delay_ms);
  }
}

}  // namespace offscreen
