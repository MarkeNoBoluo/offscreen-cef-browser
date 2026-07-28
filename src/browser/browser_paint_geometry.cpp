#include "browser/browser_paint_geometry.h"

#include <algorithm>
#include <cmath>

namespace offscreen {

/// 将 NaN、无穷或非正缩放系数回退为 1.0。
/// @param scale 原始设备缩放系数。
/// @return 可用于坐标转换的正数缩放系数。
double NormalizeDeviceScaleFactor(double scale) {
  if (!std::isfinite(scale) || scale <= 0.0) {
    return 1.0;
  }
  return scale;
}

/// 通过向外取整避免缩放转换后遗漏边缘像素。
/// @param rect CEF 脏区域的物理像素矩形。
/// @param scale 设备像素缩放系数。
/// @return Qt update 使用的 DIP 矩形。
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
