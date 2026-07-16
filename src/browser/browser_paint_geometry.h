#pragma once

#include "browser/browser_geometry.h"

namespace offscreen {

struct BrowserPhysicalRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

double NormalizeDeviceScaleFactor(double scale);

BrowserViewRect PhysicalRectToDipUpdateRect(BrowserPhysicalRect rect,
                                            double scale);

}  // namespace offscreen
