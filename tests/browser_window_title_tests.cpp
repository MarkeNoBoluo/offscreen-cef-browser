#include "app/browser_window_title.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expect_eq(const std::string& actual,
               const std::string& expected,
               const char* label) {
  if (actual != expected) {
    std::cerr << label << " expected [" << expected << "] but got [" << actual
              << "]\n";
    std::exit(1);
  }
}

void test_formats_browser_creation_failure_title_with_error_detail() {
  expect_eq(offscreen::BrowserCreationFailedTitle(
                "CefBrowserHost::CreateBrowser returned false"),
            "Offscreen CEF Browser - Browser creation failed: "
            "CefBrowserHost::CreateBrowser returned false",
            "window title");
}

}  // namespace

int main() {
  test_formats_browser_creation_failure_title_with_error_detail();
  return 0;
}
