#include "browser/browser_geometry.h"

#include <cstdlib>
#include <iostream>

namespace {

void expect_eq(int actual, int expected, const char* label) {
  if (actual != expected) {
    std::cerr << label << " expected [" << expected << "] but got [" << actual
              << "]\n";
    std::exit(1);
  }
}

void test_uses_widget_size_when_positive() {
  const offscreen::BrowserViewRect rect =
      offscreen::ViewRectFromWidgetSize(1024, 768);

  expect_eq(rect.x, 0, "x");
  expect_eq(rect.y, 0, "y");
  expect_eq(rect.width, 1024, "width");
  expect_eq(rect.height, 768, "height");
}

void test_clamps_zero_size_to_one_pixel() {
  const offscreen::BrowserViewRect rect = offscreen::ViewRectFromWidgetSize(0, 0);

  expect_eq(rect.x, 0, "x");
  expect_eq(rect.y, 0, "y");
  expect_eq(rect.width, 1, "width");
  expect_eq(rect.height, 1, "height");
}

void test_clamps_negative_size_to_one_pixel() {
  const offscreen::BrowserViewRect rect =
      offscreen::ViewRectFromWidgetSize(-10, -20);

  expect_eq(rect.x, 0, "x");
  expect_eq(rect.y, 0, "y");
  expect_eq(rect.width, 1, "width");
  expect_eq(rect.height, 1, "height");
}

}  // namespace

int main() {
  test_uses_widget_size_when_positive();
  test_clamps_zero_size_to_one_pixel();
  test_clamps_negative_size_to_one_pixel();
  return 0;
}
