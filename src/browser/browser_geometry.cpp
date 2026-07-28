#include "browser/browser_geometry.h"

#include <algorithm>

namespace offscreen {

/// 对 Qt 控件尺寸做下限保护，避免 CEF 获得零尺寸视图。
/// @param width 控件宽度。
/// @param height 控件高度。
/// @return 至少为 1x1 的逻辑视图矩形。
BrowserViewRect ViewRectFromWidgetSize(int width, int height) {
  BrowserViewRect rect;
  rect.width = std::max(width, 1);
  rect.height = std::max(height, 1);
  return rect;
}

}  // namespace offscreen
