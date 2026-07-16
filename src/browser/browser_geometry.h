#pragma once

namespace offscreen {

struct BrowserViewRect {
  int x = 0;
  int y = 0;
  int width = 1;
  int height = 1;
};

BrowserViewRect ViewRectFromWidgetSize(int width, int height);

}  // namespace offscreen
