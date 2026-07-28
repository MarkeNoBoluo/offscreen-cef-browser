#pragma once

#include "browser/browser_geometry.h"

namespace offscreen {

/// CEF OnPaint 返回的物理像素矩形。
struct BrowserPhysicalRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

/// 将无效的设备缩放系数归一化为可用值。
/// @param scale 原始缩放系数。
/// @return 大于零的缩放系数。
double NormalizeDeviceScaleFactor(double scale);

/// 将物理像素脏矩形转换为 Qt 重绘使用的 DIP 矩形。
/// @param rect CEF 返回的物理像素矩形。
/// @param scale 当前设备缩放系数。
/// @return 向外取整后的逻辑像素矩形。
BrowserViewRect PhysicalRectToDipUpdateRect(BrowserPhysicalRect rect,
                                            double scale);

}  // namespace offscreen
