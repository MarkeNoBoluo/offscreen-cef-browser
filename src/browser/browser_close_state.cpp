#include "browser/browser_close_state.h"

namespace offscreen {

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
