#include "browser/browser_paint_geometry.h"

#include <cmath>
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

void expect_true(bool actual, const char* label) {
  if (!actual) {
    std::cerr << label << " expected true but got false\n";
    std::exit(1);
  }
}

// --- NormalizeDeviceScaleFactor ---

void test_normalize_scale_returns_positive_values_unchanged() {
  expect_true(std::abs(offscreen::NormalizeDeviceScaleFactor(1.0) - 1.0) < 0.001,
              "scale 1.0");
  expect_true(
      std::abs(offscreen::NormalizeDeviceScaleFactor(1.25) - 1.25) < 0.001,
      "scale 1.25");
  expect_true(std::abs(offscreen::NormalizeDeviceScaleFactor(1.5) - 1.5) < 0.001,
              "scale 1.5");
  expect_true(std::abs(offscreen::NormalizeDeviceScaleFactor(2.0) - 2.0) < 0.001,
              "scale 2.0");
}

void test_normalize_scale_clamps_zero_to_one() {
  expect_true(std::abs(offscreen::NormalizeDeviceScaleFactor(0.0) - 1.0) < 0.001,
              "scale 0");
}

void test_normalize_scale_clamps_negative_to_one() {
  expect_true(
      std::abs(offscreen::NormalizeDeviceScaleFactor(-1.0) - 1.0) < 0.001,
      "scale -1.0");
  expect_true(
      std::abs(offscreen::NormalizeDeviceScaleFactor(-0.5) - 1.0) < 0.001,
      "scale -0.5");
}

void test_normalize_scale_clamps_nan_to_one() {
  const double nan = std::nan("");
  expect_true(std::abs(offscreen::NormalizeDeviceScaleFactor(nan) - 1.0) < 0.001,
              "scale NaN");
}

void test_normalize_scale_clamps_inf_to_one() {
  const double inf = std::numeric_limits<double>::infinity();
  expect_true(std::abs(offscreen::NormalizeDeviceScaleFactor(inf) - 1.0) < 0.001,
              "scale inf");
  expect_true(
      std::abs(offscreen::NormalizeDeviceScaleFactor(-inf) - 1.0) < 0.001,
      "scale -inf");
}

// --- PhysicalRectToDipUpdateRect ---

void test_physical_to_dip_scale_1_dot_0_exact() {
  const offscreen::BrowserPhysicalRect phys{0, 0, 100, 200};
  const offscreen::BrowserViewRect dip =
      offscreen::PhysicalRectToDipUpdateRect(phys, 1.0);
  expect_eq(dip.x, 0, "x");
  expect_eq(dip.y, 0, "y");
  expect_eq(dip.width, 100, "width");
  expect_eq(dip.height, 200, "height");
}

void test_physical_to_dip_scale_1_dot_25_outward_expand() {
  // physical: x=0, w=125 -> dip: floor(0/1.25)=0, ceil(125/1.25)=100 -> w=100
  const offscreen::BrowserPhysicalRect phys{0, 0, 125, 250};
  const offscreen::BrowserViewRect dip =
      offscreen::PhysicalRectToDipUpdateRect(phys, 1.25);
  expect_eq(dip.x, 0, "x");
  expect_eq(dip.y, 0, "y");
  expect_eq(dip.width, 100, "width");
  expect_eq(dip.height, 200, "height");
}

void test_physical_to_dip_scale_2_dot_0_outward_expand() {
  // physical: x=1, w=2 -> dip: floor(1/2)=0, ceil(3/2)=2 -> w=2
  const offscreen::BrowserPhysicalRect phys{1, 1, 2, 2};
  const offscreen::BrowserViewRect dip =
      offscreen::PhysicalRectToDipUpdateRect(phys, 2.0);
  expect_eq(dip.x, 0, "x");
  expect_eq(dip.y, 0, "y");
  expect_eq(dip.width, 2, "width");
  expect_eq(dip.height, 2, "height");
}

void test_physical_to_dip_scale_1_dot_5_odd_offset() {
  // physical: x=3, w=5 -> physical end = 8
  // floor(3/1.5)=2, ceil(8/1.5)=ceil(5.333...)=6 -> w=4
  const offscreen::BrowserPhysicalRect phys{3, 3, 5, 5};
  const offscreen::BrowserViewRect dip =
      offscreen::PhysicalRectToDipUpdateRect(phys, 1.5);
  expect_eq(dip.x, 2, "x");
  expect_eq(dip.y, 2, "y");
  expect_eq(dip.width, 4, "width");
  expect_eq(dip.height, 4, "height");
}

void test_physical_to_dip_minimum_one_pixel() {
  // physical: x=0, w=0 should yield w=1 minimum
  const offscreen::BrowserPhysicalRect phys{0, 0, 0, 0};
  const offscreen::BrowserViewRect dip =
      offscreen::PhysicalRectToDipUpdateRect(phys, 1.0);
  expect_eq(dip.width, 1, "width zero");
  expect_eq(dip.height, 1, "height zero");
}

}  // namespace

int main() {
  test_normalize_scale_returns_positive_values_unchanged();
  test_normalize_scale_clamps_zero_to_one();
  test_normalize_scale_clamps_negative_to_one();
  test_normalize_scale_clamps_nan_to_one();
  test_normalize_scale_clamps_inf_to_one();
  test_physical_to_dip_scale_1_dot_0_exact();
  test_physical_to_dip_scale_1_dot_25_outward_expand();
  test_physical_to_dip_scale_2_dot_0_outward_expand();
  test_physical_to_dip_scale_1_dot_5_odd_offset();
  test_physical_to_dip_minimum_one_pixel();
  return 0;
}
