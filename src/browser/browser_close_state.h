#pragma once

namespace offscreen {

/// BrowserService 根据当前 CEF 生命周期选择的关闭步骤。
enum class BrowserCloseAction {
  AllowQtClose,
  WaitForBrowserCreation,
  StartBrowserClose,
  WaitForBrowserClose,
};

/// 根据浏览器、客户端和关闭标记决定下一步关闭动作。
/// @param has_browser 是否已有 CEF 浏览器对象。
/// @param has_client 是否已创建 CEF 客户端但浏览器可能尚未完成创建。
/// @param is_closing 是否已发起过关闭。
/// @return 应由调用方执行的关闭动作。
BrowserCloseAction DecideBrowserCloseAction(bool has_browser,
                                            bool has_client,
                                            bool is_closing);

}  // namespace offscreen
