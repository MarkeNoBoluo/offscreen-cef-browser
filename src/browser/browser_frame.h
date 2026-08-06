#pragma once

#include <memory>
#include <mutex>

#include <QImage>

#include "browser/browser_geometry.h"
#include "browser/browser_paint_geometry.h"

namespace offscreen {

struct BrowserFrameSnapshot {
  /// 是否已收到主视图像素帧。
  bool has_view = false;
  /// CEF 弹出层当前是否可见。
  bool popup_visible = false;
  /// 弹出层相对主视图的逻辑像素位置。
  BrowserViewRect popup_rect;
  /// 主视图的共享不可变帧引用，绘制端不得修改（调用非 const 方法会触发
  /// detach 导致性能回归）。
  QImage view_image;
  /// 弹出层的共享不可变帧引用，绘制端不得修改（同上）。
  QImage popup_image;
};

// 保存最新的视图帧和弹出层帧。CEF 提供的缓冲区只在 OnPaint 回调期间有效，
// 因此写入时必须复制；写入后帧数据永不再原地修改，绘制端通过 Snapshot 共享
// 同一不可变 QImage（隐式共享仅 bump 原子引用计数，O(1) 浅拷贝）。
class BrowserFrame {
 public:
  /// 创建空帧缓存。
  BrowserFrame() = default;

  /// 复制 CEF 主视图的 BGRA 像素。
  /// @param bgra_buffer 仅在调用期间有效的 CEF 像素缓冲区。
  /// @param width 像素宽度。
  /// @param height 像素高度。
  /// @param scale 设备像素缩放系数。
  void SetViewImage(const void* bgra_buffer, int width, int height, double scale);
  /// 复制 CEF 弹出层的 BGRA 像素。
  /// @param bgra_buffer 仅在调用期间有效的 CEF 像素缓冲区。
  /// @param width 像素宽度。
  /// @param height 像素高度。
  /// @param scale 设备像素缩放系数。
  void SetPopupImage(const void* bgra_buffer, int width, int height, double scale);
  /// 更新弹出层可见状态。
  /// @param visible CEF 报告的显示状态。
  void SetPopupVisible(bool visible);
  /// 更新弹出层相对主视图的位置。
  /// @param rect CEF 提供的逻辑像素矩形。
  void SetPopupRect(BrowserViewRect rect);
  /// 读取可在 Qt 绘制线程安全使用的帧快照。
  /// @return 主视图和弹出层的当前快照；图像字段为共享不可变帧引用，绘制端只读。
  BrowserFrameSnapshot Snapshot() const;

 private:
  mutable std::mutex mutex_;
  QImage view_image_;
  QImage popup_image_;
  bool popup_visible_ = false;
  BrowserViewRect popup_rect_;
};

}  // namespace offscreen
