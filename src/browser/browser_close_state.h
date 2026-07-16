#pragma once

namespace offscreen {

enum class BrowserCloseAction {
  AllowQtClose,
  WaitForBrowserCreation,
  StartBrowserClose,
  WaitForBrowserClose,
};

BrowserCloseAction DecideBrowserCloseAction(bool has_browser,
                                            bool has_client,
                                            bool is_closing);

}  // namespace offscreen
