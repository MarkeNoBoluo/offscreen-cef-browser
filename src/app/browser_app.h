#pragma once

#include <cstdint>
#include <functional>

#include "include/cef_app.h"
#include "include/cef_browser_process_handler.h"

namespace offscreen {

class BrowserApp final : public CefApp, public CefBrowserProcessHandler {
 public:
  using MessagePumpScheduler = std::function<void(int64_t delay_ms)>;

  explicit BrowserApp(MessagePumpScheduler message_pump_scheduler = {});
  void SetMessagePumpScheduler(MessagePumpScheduler message_pump_scheduler);

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override;
  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override;
  void OnScheduleMessagePumpWork(int64_t delay_ms) override;

 private:
  MessagePumpScheduler message_pump_scheduler_;

  IMPLEMENT_REFCOUNTING(BrowserApp);
};

}  // namespace offscreen
