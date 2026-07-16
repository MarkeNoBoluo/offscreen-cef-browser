#include "browser/browser_geometry.h"

#include <algorithm>

namespace offscreen {

BrowserViewRect ViewRectFromWidgetSize(int width, int height) {
  BrowserViewRect rect;
  rect.width = std::max(width, 1);
  rect.height = std::max(height, 1);
  return rect;
}

}  // namespace offscreen
