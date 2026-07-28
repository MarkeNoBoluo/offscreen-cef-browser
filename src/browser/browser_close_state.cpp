#include "browser/browser_close_state.h"

namespace offscreen {

/// 避免在浏览器异步创建和关闭的中间状态提前销毁 Qt 控件。
/// @param has_browser 是否已有 CEF 浏览器。
/// @param has_client 是否已有等待回调的 CEF 客户端。
/// @param is_closing 是否已请求关闭。
/// @return 调用方下一步应执行的关闭动作。
BrowserCloseAction DecideBrowserCloseAction(bool has_browser,
                                            bool has_client,
                                            bool is_closing) {
  if (has_browser) {
    return is_closing ? BrowserCloseAction::WaitForBrowserClose
                      : BrowserCloseAction::StartBrowserClose;
  }
  return has_client ? BrowserCloseAction::WaitForBrowserCreation
                    : BrowserCloseAction::AllowQtClose;
}

}  // namespace offscreen
