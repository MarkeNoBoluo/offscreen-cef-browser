#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "browser/browser_frame.h"
#include "browser/browser_paint_geometry.h"
#include "include/cef_render_handler.h"

namespace offscreen {

class RenderStats;

using PaintUpdateCallback =
    std::function<void(const std::vector<BrowserViewRect>&)>;
using ImeCompositionRangeChangedCallback =
    std::function<void(const CefRange&, const std::vector<CefRect>&)>;

// CEF 的离屏渲染入口。它将 BGRA 像素帧存入 BrowserFrame，并只通知 Qt
// 需要重绘的 DIP 脏区域；mutex 保护 CEF 回调与 Qt 尺寸更新之间的共享状态。
class OsrRenderHandler final : public CefRenderHandler {
 public:
  using StartDraggingCallback =
      std::function<bool(CefRefPtr<CefBrowser>,
                         CefRefPtr<CefDragData>,
                         CefRenderHandler::DragOperationsMask,
                         int,
                         int)>;
  using UpdateDragCursorCallback =
      std::function<void(CefRefPtr<CefBrowser>,
                         CefRenderHandler::DragOperation)>;

  /// 创建离屏渲染处理器。
  /// @param view_rect 初始逻辑视图矩形。
  /// @param device_scale_factor 初始设备缩放系数。
  /// @param frame 保存像素帧的共享缓存。
  /// @param paint_update_callback 通知 Qt 脏区域的回调。
  OsrRenderHandler(BrowserViewRect view_rect,
                   double device_scale_factor,
                   std::shared_ptr<BrowserFrame> frame,
                   PaintUpdateCallback paint_update_callback,
                   std::shared_ptr<RenderStats> render_stats);

  /// 更新 CEF 查询的视图尺寸与缩放。
  /// @param view_rect 最新逻辑视图矩形。
  /// @param device_scale_factor 最新设备缩放系数。
  void SetViewRect(BrowserViewRect view_rect, double device_scale_factor);
  /// 返回当前逻辑视图矩形。
  /// @return 已加锁复制的视图矩形。
  BrowserViewRect view_rect() const;
  /// 设置输入法组合范围变更回调。
  /// @param callback 接收 CEF 选择范围和字符矩形的回调。
  void SetImeCompositionRangeChangedCallback(
      ImeCompositionRangeChangedCallback callback);
  void SetStartDraggingCallback(StartDraggingCallback callback);
  void SetUpdateDragCursorCallback(UpdateDragCursorCallback callback);

  /// 向 CEF 提供逻辑视图矩形。
  /// @param browser 发起查询的浏览器。
  /// @param rect 输出视图矩形。
  void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
  /// 向 CEF 提供设备缩放和可用屏幕区域。
  /// @param browser 发起查询的浏览器。
  /// @param screen_info 输出屏幕信息。
  /// @return 始终为 true，表示信息有效。
  bool GetScreenInfo(CefRefPtr<CefBrowser> browser,
                     CefScreenInfo& screen_info) override;
  /// 切换 CEF 下拉菜单等弹出层的可见状态。
  /// @param browser 产生弹出层的浏览器。
  /// @param show 是否显示弹出层。
  void OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) override;
  /// 更新 CEF 弹出层的像素位置和大小。
  /// @param browser 产生弹出层的浏览器。
  /// @param rect 弹出层矩形。
  void OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect& rect) override;
  /// 接收 CEF 主视图或弹出层像素，并通知 Qt 重绘。
  /// @param browser 产生像素帧的浏览器。
  /// @param type 主视图或弹出层类型。
  /// @param dirtyRects 本次变化的物理像素区域。
  /// @param buffer BGRA 像素缓冲区。
  /// @param width 像素宽度。
  /// @param height 像素高度。
  void OnPaint(CefRefPtr<CefBrowser> browser,
               PaintElementType type,
               const RectList& dirtyRects,
               const void* buffer,
               int width,
               int height) override;
  bool StartDragging(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefDragData> drag_data,
                     DragOperationsMask allowed_ops,
                     int x,
                     int y) override;
  void UpdateDragCursor(CefRefPtr<CefBrowser> browser,
                        DragOperation operation) override;
  /// 转发 CEF 的组合文本选择范围与字符位置。
  /// @param browser 产生输入法状态的浏览器。
  /// @param selected_range 当前选择范围。
  /// @param character_bounds 每个字符的逻辑像素边界。
  void OnImeCompositionRangeChanged(
      CefRefPtr<CefBrowser> browser,
      const CefRange& selected_range,
      const RectList& character_bounds) override;

 private:
  mutable std::mutex mutex_;
  BrowserViewRect view_rect_;
  double device_scale_factor_ = 1.0;
  std::shared_ptr<BrowserFrame> frame_;
  std::shared_ptr<RenderStats> render_stats_;
  PaintUpdateCallback paint_update_callback_;
  ImeCompositionRangeChangedCallback ime_composition_range_changed_callback_;
  StartDraggingCallback start_dragging_callback_;
  UpdateDragCursorCallback update_drag_cursor_callback_;

  IMPLEMENT_REFCOUNTING(OsrRenderHandler);
};

}  // namespace offscreen
