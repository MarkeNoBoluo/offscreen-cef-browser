#include "app/browser_app.h"

#include <atomic>
#include <sstream>
#include <utility>

#include "app/diagnostic_log.h"
#include "include/cef_command_line.h"

namespace offscreen {

/// 保存可选的外部消息泵调度器。
/// @param message_pump_scheduler 延迟调度回调。
BrowserApp::BrowserApp(MessagePumpScheduler message_pump_scheduler)
    : message_pump_scheduler_(std::move(message_pump_scheduler)) {}

/// 替换 CEF 消息泵的宿主调度器。
/// @param message_pump_scheduler 新的延迟调度回调。
void BrowserApp::SetMessagePumpScheduler(
    MessagePumpScheduler message_pump_scheduler) {
  message_pump_scheduler_ = std::move(message_pump_scheduler);
}

/// 将进程级 CEF 回调路由回当前对象。
/// @return 当前浏览器进程处理器。
CefRefPtr<CefBrowserProcessHandler> BrowserApp::GetBrowserProcessHandler() {
  return this;
}

/// 仅为主进程补充 Alloy UI、GPU 和硬件解码开关。
/// @param process_type 当前 CEF 进程类型。
/// @param command_line 可修改的 CEF 命令行。
void BrowserApp::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
  DiagnosticLog("BrowserApp::OnBeforeCommandLineProcessing process_type=[" +
                process_type.ToString() + "]");
  if (!process_type.empty()) {
    return;
  }

  command_line->AppendSwitch("use-alloy-style");
  command_line->AppendSwitch("enable-gpu");
  command_line->AppendSwitch("enable-accelerated-video-decode");
  command_line->AppendSwitchWithValue("use-angle", "d3d11");
  DiagnosticLog("BrowserApp appended switches: use-alloy-style, enable-gpu, "
                "enable-accelerated-video-decode, use-angle=d3d11");
}

/// 将 CEF 消息泵请求转发给 Qt 调度器。
/// @param delay_ms CEF 指定的延迟毫秒数。
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
