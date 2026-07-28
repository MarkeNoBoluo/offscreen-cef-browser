#pragma once

#include <cstdint>
#include <functional>

#include "include/cef_app.h"
#include "include/cef_browser_process_handler.h"

namespace offscreen {

/// CEF 进程级回调实现，负责命令行开关和外部消息泵调度。
class BrowserApp final : public CefApp, public CefBrowserProcessHandler {
 public:
  using MessagePumpScheduler = std::function<void(int64_t delay_ms)>;

  /// 创建 CEF 应用对象。
  /// @param message_pump_scheduler 接收 CEF 下一次消息泵工作的延迟毫秒数。
  explicit BrowserApp(MessagePumpScheduler message_pump_scheduler = {});
  /// 替换外部消息泵调度器。
  /// @param message_pump_scheduler 新的延迟调度回调。
  void SetMessagePumpScheduler(MessagePumpScheduler message_pump_scheduler);

  /// 返回浏览器进程处理器，CEF 通过此对象派发进程级回调。
  /// @return 当前对象的 CEF 引用。
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override;
  /// 配置浏览器进程命令行参数。
  /// @param process_type 当前进程类型，空值表示浏览器主进程。
  /// @param command_line 可写的 CEF 命令行对象。
  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override;
  /// 接收 CEF 请求的下一次消息泵执行时间。
  /// @param delay_ms 距离执行 CefDoMessageLoopWork 的延迟毫秒数。
  void OnScheduleMessagePumpWork(int64_t delay_ms) override;

 private:
  MessagePumpScheduler message_pump_scheduler_;

  IMPLEMENT_REFCOUNTING(BrowserApp);
};

}  // namespace offscreen
