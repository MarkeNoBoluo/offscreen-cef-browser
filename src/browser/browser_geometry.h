#pragma once

namespace offscreen {

/// CEF 与 Qt 共用的逻辑像素视图矩形。
struct BrowserViewRect {
  int x = 0;
  int y = 0;
  int width = 1;
  int height = 1;
};

/// 将 Qt 控件尺寸转换为至少为 1x1 的浏览器视图矩形。
/// @param width 控件宽度。
/// @param height 控件高度。
/// @return 可安全交给 CEF 的视图矩形。
BrowserViewRect ViewRectFromWidgetSize(int width, int height);

}  // namespace offscreen
