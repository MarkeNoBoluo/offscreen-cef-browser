#include "app/app_config.h"

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

void test_uses_default_url_when_argument_is_missing() {
  const char* argv[] = {"offscreen_cef_browser"};

  const offscreen::AppConfig config = offscreen::AppConfig::FromArgs(1, argv);

  expect_eq(config.initial_url, "https://baidu.com/", "initial_url");
}

void test_uses_url_argument_when_present() {
  const char* argv[] = {"offscreen_cef_browser", "--url=https://example.org/path"};

  const offscreen::AppConfig config = offscreen::AppConfig::FromArgs(2, argv);

  expect_eq(config.initial_url, "https://example.org/path", "initial_url");
}

void test_ignores_empty_url_argument() {
  const char* argv[] = {"offscreen_cef_browser", "--url="};

  const offscreen::AppConfig config = offscreen::AppConfig::FromArgs(2, argv);

  expect_eq(config.initial_url, "https://baidu.com/", "initial_url");
}

}  // namespace

int main() {
  test_uses_default_url_when_argument_is_missing();
  test_uses_url_argument_when_present();
  test_ignores_empty_url_argument();
  return 0;
}
