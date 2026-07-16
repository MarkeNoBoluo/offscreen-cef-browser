#include "browser/browser_paint_geometry.h"

#include <algorithm>
#include <cmath>

namespace offscreen {

double NormalizeDeviceScaleFactor(double scale) {
  if (!std::isfinite(scale) || scale <= 0.0) {
    return 1.0;
  }
  return scale;
}

BrowserViewRect PhysicalRectToDipUpdateRect(BrowserPhysicalRect rect,
                                            double scale) {
  const double safe_scale = NormalizeDeviceScaleFactor(scale);
  const int dip_x = static_cast<int>(std::floor(rect.x / safe_scale));
  const int dip_y = static_cast<int>(std::floor(rect.y / safe_scale));
  const int dip_right =
      static_cast<int>(std::ceil((rect.x + rect.width) / safe_scale));
  const int dip_bottom =
      static_cast<int>(std::ceil((rect.y + rect.height) / safe_scale));
  BrowserViewRect result;
  result.x = dip_x;
  result.y = dip_y;
  result.width = std::max(dip_right - dip_x, 1);
  result.height = std::max(dip_bottom - dip_y, 1);
  return result;
}

}  // namespace offscreen
