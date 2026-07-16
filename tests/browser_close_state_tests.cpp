#include "browser/browser_close_state.h"

#include <cstdlib>
#include <iostream>

namespace {

void expect_eq(offscreen::BrowserCloseAction actual,
               offscreen::BrowserCloseAction expected,
               const char* label) {
  if (actual != expected) {
    std::cerr << label << " expected [" << static_cast<int>(expected)
              << "] but got [" << static_cast<int>(actual) << "]\n";
    std::exit(1);
  }
}

void test_allows_qt_close_when_no_browser_or_client_exists() {
  expect_eq(offscreen::DecideBrowserCloseAction(false, false, false),
            offscreen::BrowserCloseAction::AllowQtClose,
            "close action");
}

void test_waits_when_client_exists_before_browser_is_created() {
  expect_eq(offscreen::DecideBrowserCloseAction(false, true, false),
            offscreen::BrowserCloseAction::WaitForBrowserCreation,
            "close action");
}

void test_starts_browser_close_when_browser_exists_and_not_closing() {
  expect_eq(offscreen::DecideBrowserCloseAction(true, true, false),
            offscreen::BrowserCloseAction::StartBrowserClose,
            "close action");
}

void test_waits_without_reinvoking_close_when_already_closing() {
  expect_eq(offscreen::DecideBrowserCloseAction(true, true, true),
            offscreen::BrowserCloseAction::WaitForBrowserClose,
            "close action");
}

}  // namespace

int main() {
  test_allows_qt_close_when_no_browser_or_client_exists();
  test_waits_when_client_exists_before_browser_is_created();
  test_starts_browser_close_when_browser_exists_and_not_closing();
  test_waits_without_reinvoking_close_when_already_closing();
  return 0;
}
