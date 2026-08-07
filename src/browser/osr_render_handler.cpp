#include "browser/osr_render_handler.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>

#include <dxgi.h>

#include "app/diagnostic_log.h"
#include "browser/osr_render_log.h"
#include "browser/render_stats.h"

namespace offscreen {

OsrRenderHandler::OsrRenderHandler(
    BrowserViewRect view_rect,
    double device_scale_factor,
    std::shared_ptr<BrowserFrame> frame,
    PaintUpdateCallback paint_update_callback,
    std::shared_ptr<RenderStats> render_stats)
    : view_rect_(view_rect),
      device_scale_factor_(NormalizeDeviceScaleFactor(device_scale_factor)),
      frame_(std::move(frame)),
      render_stats_(std::move(render_stats)),
      paint_update_callback_(std::move(paint_update_callback)) {
  std::ostringstream stream;
  stream << "OsrRenderHandler constructed rect=" << view_rect_.x << ","
         << view_rect_.y << " " << view_rect_.width << "x"
         << view_rect_.height << " scale=" << device_scale_factor_
         << " frame=" << HexValue(reinterpret_cast<uintptr_t>(frame_.get()))
         << " has_paint_callback="
         << (paint_update_callback_ ? "true" : "false");
  DiagnosticLog(stream.str());
}

void OsrRenderHandler::SetViewRect(BrowserViewRect view_rect,
                                    double device_scale_factor) {
  std::lock_guard<std::mutex> lock(mutex_);
  view_rect_ = view_rect;
  device_scale_factor_ = NormalizeDeviceScaleFactor(device_scale_factor);
  OsrRenderLogRecord record;
  record.event = "SetViewRect";
  record.x = view_rect_.x;
  record.y = view_rect_.y;
  record.w = view_rect_.width;
  record.h = view_rect_.height;
  record.scale = device_scale_factor_;
  OsrRenderLogWrite(record);
}

BrowserViewRect OsrRenderHandler::view_rect() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return view_rect_;
}

void OsrRenderHandler::SetImeCompositionRangeChangedCallback(
    ImeCompositionRangeChangedCallback callback) {
  DiagnosticLog("OsrRenderHandler::SetImeCompositionRangeChangedCallback");
  ime_composition_range_changed_callback_ = std::move(callback);
}

void OsrRenderHandler::SetStartDraggingCallback(
    StartDraggingCallback callback) {
  DiagnosticLog("OsrRenderHandler::SetStartDraggingCallback");
  start_dragging_callback_ = std::move(callback);
}

void OsrRenderHandler::SetUpdateDragCursorCallback(
    UpdateDragCursorCallback callback) {
  DiagnosticLog("OsrRenderHandler::SetUpdateDragCursorCallback");
  update_drag_cursor_callback_ = std::move(callback);
}

void OsrRenderHandler::GetViewRect(CefRefPtr<CefBrowser> browser,
                                    CefRect& rect) {
  std::lock_guard<std::mutex> lock(mutex_);
  rect = CefRect(view_rect_.x, view_rect_.y, view_rect_.width, view_rect_.height);
  OsrRenderLogRecord record;
  record.event = "GetViewRect";
  record.browser_id = browser ? browser->GetIdentifier() : -1;
  record.x = rect.x;
  record.y = rect.y;
  record.w = rect.width;
  record.h = rect.height;
  OsrRenderLogWrite(record);
}

bool OsrRenderHandler::GetScreenInfo(CefRefPtr<CefBrowser> browser,
                                      CefScreenInfo& screen_info) {
  std::lock_guard<std::mutex> lock(mutex_);
  screen_info.device_scale_factor = static_cast<float>(device_scale_factor_);
  screen_info.rect = CefRect(view_rect_.x, view_rect_.y, view_rect_.width,
                              view_rect_.height);
  screen_info.available_rect = screen_info.rect;
  OsrRenderLogRecord record;
  record.event = "GetScreenInfo";
  record.browser_id = browser ? browser->GetIdentifier() : -1;
  record.x = screen_info.rect.x;
  record.y = screen_info.rect.y;
  record.w = screen_info.rect.width;
  record.h = screen_info.rect.height;
  record.scale = screen_info.device_scale_factor;
  OsrRenderLogWrite(record);
  return true;
}

void OsrRenderHandler::OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) {
  OsrRenderLogRecord record;
  record.event = "OnPopupShow";
  record.browser_id = browser ? browser->GetIdentifier() : -1;
  record.show = show ? "true" : "false";
  OsrRenderLogWrite(record);
  if (frame_) {
    frame_->SetPopupVisible(show);
  }
  if (!show && paint_update_callback_) {
    paint_update_callback_({});
  }
}

void OsrRenderHandler::OnPopupSize(CefRefPtr<CefBrowser> browser,
                                    const CefRect& rect) {
  OsrRenderLogRecord record;
  record.event = "OnPopupSize";
  record.browser_id = browser ? browser->GetIdentifier() : -1;
  record.x = rect.x;
  record.y = rect.y;
  record.w = rect.width;
  record.h = rect.height;
  OsrRenderLogWrite(record);
  if (frame_) {
    frame_->SetPopupRect(
        BrowserViewRect{rect.x, rect.y, rect.width, rect.height});
  }
}

void OsrRenderHandler::OnPaint(CefRefPtr<CefBrowser> browser,
                                PaintElementType type,
                                const RectList& dirtyRects,
                                const void* buffer,
                                int width,
                                int height) {
  if (!frame_ || !buffer || width <= 0 || height <= 0) {
    DiagnosticLog("OsrRenderHandler::OnPaint ignored invalid input frame=" +
                  HexValue(reinterpret_cast<uintptr_t>(frame_.get())) +
                  " buffer=" +
                  HexValue(reinterpret_cast<uintptr_t>(buffer)) +
                  " width=" + std::to_string(width) +
                  " height=" + std::to_string(height));
    return;
  }

  double scale;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    scale = device_scale_factor_;
  }

  int64_t dirty_area_px = 0;
  for (const auto& r : dirtyRects) {
    dirty_area_px += static_cast<int64_t>(r.width) * r.height;
  }
  if (render_stats_) {
    render_stats_->OnPaintBegin(width, height,
                                static_cast<int>(dirtyRects.size()),
                                dirty_area_px, type == PET_POPUP);
  }

  OsrRenderLogRecord record;
  record.event = "OnPaint";
  record.browser_id = browser ? browser->GetIdentifier() : -1;
  record.w = width;
  record.h = height;
  record.scale = scale;
  record.type = type == PET_VIEW ? "PET_VIEW" : "PET_POPUP";
  record.dirty_count = static_cast<int>(dirtyRects.size());
  record.dirty_area_px = dirty_area_px;
  if (!dirtyRects.empty()) {
    const auto& first = dirtyRects.front();
    std::ostringstream detail;
    detail << "first_dirty=" << first.x << "," << first.y << " "
           << first.width << "x" << first.height;
    record.detail = detail.str();
  }
  OsrRenderLogWrite(record);

  std::vector<BrowserViewRect> dip_rects;

  if (type == PET_VIEW) {
    if (render_stats_) {
      render_stats_->OnSetViewImageBegin();
    }
    frame_->SetViewImage(buffer, width, height, scale);
    if (render_stats_) {
      render_stats_->OnSetViewImageDone();
    }
    for (const auto& r : dirtyRects) {
      BrowserPhysicalRect phys{r.x, r.y, r.width, r.height};
      dip_rects.push_back(PhysicalRectToDipUpdateRect(phys, scale));
    }
  } else if (type == PET_POPUP) {
    if (render_stats_) {
      render_stats_->OnSetViewImageBegin();
    }
    frame_->SetPopupImage(buffer, width, height, scale);
    if (render_stats_) {
      render_stats_->OnSetViewImageDone();
    }
    dip_rects.push_back({});
  } else {
    return;
  }

  if (paint_update_callback_) {
    paint_update_callback_(dip_rects);
  }

  if (render_stats_) {
    render_stats_->OnPaintEnd();
  }
}

void OsrRenderHandler::OnAcceleratedPaint(CefRefPtr<CefBrowser> browser,
                                          PaintElementType type,
                                          const RectList& dirtyRects,
                                          void* shared_handle) {
  double scale;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    scale = device_scale_factor_;
  }

  int width = 0;
  int height = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    width = view_rect_.width;
    height = view_rect_.height;
  }

  int64_t dirty_area_px = 0;
  for (const auto& r : dirtyRects) {
    dirty_area_px += static_cast<int64_t>(r.width) * r.height;
  }
  if (render_stats_) {
    render_stats_->OnAcceleratedPaintBegin(
        width, height, static_cast<int>(dirtyRects.size()), dirty_area_px,
        type == PET_POPUP);
  }

  OsrRenderLogRecord record;
  record.event = "OnAcceleratedPaint";
  record.browser_id = browser ? browser->GetIdentifier() : -1;
  record.w = width;
  record.h = height;
  record.scale = scale;
  record.type = type == PET_VIEW ? "ACCELERATED_VIEW" : "ACCELERATED_POPUP";
  record.dirty_count = static_cast<int>(dirtyRects.size());
  record.dirty_area_px = dirty_area_px;
  {
    std::ostringstream detail;
    detail << "shared_handle="
           << HexValue(reinterpret_cast<uintptr_t>(shared_handle));
    record.detail = detail.str();
  }
  OsrRenderLogWrite(record);

  // 读取共享纹理（OpenSharedResource → staging → Map）并复用
  // SetViewImage/SetPopupImage 更新 BrowserFrame，使 Qt 侧照常显示。
  const bool read_ok = ReadSharedTexture(type, dirtyRects, shared_handle, scale);
  if (read_ok) {
    record.detail = "shared_handle=" +
                    HexValue(reinterpret_cast<uintptr_t>(shared_handle)) +
                    " read=ok";
  } else {
    record.detail = "shared_handle=" +
                    HexValue(reinterpret_cast<uintptr_t>(shared_handle)) +
                    " read=fail";
  }
  OsrRenderLogWrite(record);

  std::vector<BrowserViewRect> dip_rects;
  for (const auto& r : dirtyRects) {
    BrowserPhysicalRect phys{r.x, r.y, r.width, r.height};
    dip_rects.push_back(PhysicalRectToDipUpdateRect(phys, scale));
  }
  if (paint_update_callback_) {
    paint_update_callback_(dip_rects);
  }

  if (render_stats_) {
    render_stats_->OnPaintEnd();
  }
}

bool OsrRenderHandler::StartDragging(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefDragData> drag_data,
                                     DragOperationsMask allowed_ops,
                                     int x,
                                     int y) {
  std::ostringstream stream;
  stream << "OsrRenderHandler::StartDragging browser_id="
         << (browser ? browser->GetIdentifier() : -1)
         << " has_drag_data=" << (drag_data ? "true" : "false")
         << " allowed_ops=" << allowed_ops << " screen=" << x << "," << y
         << " has_callback="
         << (start_dragging_callback_ ? "true" : "false");
  DiagnosticLog(stream.str());

  if (!start_dragging_callback_) {
    return false;
  }
  return start_dragging_callback_(browser, drag_data, allowed_ops, x, y);
}

void OsrRenderHandler::UpdateDragCursor(CefRefPtr<CefBrowser> browser,
                                        DragOperation operation) {
  DiagnosticLog("OsrRenderHandler::UpdateDragCursor browser_id=" +
                std::to_string(browser ? browser->GetIdentifier() : -1) +
                " operation=" + std::to_string(operation) +
                " has_callback=" +
                (update_drag_cursor_callback_ ? "true" : "false"));
  if (update_drag_cursor_callback_) {
    update_drag_cursor_callback_(browser, operation);
  }
}

void OsrRenderHandler::OnImeCompositionRangeChanged(
    CefRefPtr<CefBrowser> browser,
    const CefRange& selected_range,
    const RectList& character_bounds) {
  (void)browser;
  std::ostringstream stream;
  stream << "OsrRenderHandler::OnImeCompositionRangeChanged selected="
         << selected_range.from << "-" << selected_range.to
         << " bounds=" << character_bounds.size();
  if (!character_bounds.empty()) {
    const auto& first = character_bounds.front();
    stream << " first=" << first.x << "," << first.y << " " << first.width
           << "x" << first.height;
  }
  DiagnosticLog(stream.str());
  if (ime_composition_range_changed_callback_) {
    ime_composition_range_changed_callback_(selected_range, character_bounds);
  }
}

bool OsrRenderHandler::EnsureD3D11Device() {
  if (d3d11_device_) {
    return true;
  }
  const D3D_FEATURE_LEVEL feature_levels[] = {
      D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  const HRESULT hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, feature_levels,
      static_cast<UINT>(sizeof(feature_levels) / sizeof(feature_levels[0])),
      D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
      context.GetAddressOf());
  if (FAILED(hr)) {
    std::ostringstream stream;
    stream << "OsrRenderHandler::EnsureD3D11Device D3D11CreateDevice failed hr=0x"
           << std::hex << static_cast<uint32_t>(hr);
    DiagnosticLog(stream.str());
    return false;
  }
  d3d11_device_ = std::move(device);
  d3d11_context_ = std::move(context);
  DiagnosticLog("OsrRenderHandler::EnsureD3D11Device created hardware device");
  return true;
}

bool OsrRenderHandler::OpenSharedTexture(void* shared_handle,
                                         ID3D11Texture2D** out_texture) {
  if (!out_texture) {
    return false;
  }
  *out_texture = nullptr;
  if (!d3d11_device_) {
    return false;
  }
  if (SUCCEEDED(d3d11_device_->OpenSharedResource(
          shared_handle, __uuidof(ID3D11Texture2D),
          reinterpret_cast<void**>(out_texture)))) {
    return true;
  }

  // 默认适配器无法打开共享句柄（多 GPU 适配器不匹配），枚举全部适配器重试。
  DiagnosticLog("OsrRenderHandler::OpenSharedTexture default adapter failed; "
                "enumerating adapters");
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  HRESULT hr = CreateDXGIFactory1(
      __uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.GetAddressOf()));
  if (FAILED(hr)) {
    return false;
  }
  const D3D_FEATURE_LEVEL feature_levels[] = {
      D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
  for (UINT index = 0;; ++index) {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(index, adapter.GetAddressOf()) ==
        DXGI_ERROR_NOT_FOUND) {
      break;
    }
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(
            adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, feature_levels,
            static_cast<UINT>(sizeof(feature_levels) / sizeof(feature_levels[0])),
            D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
            context.GetAddressOf()))) {
      continue;
    }
    if (SUCCEEDED(device->OpenSharedResource(
            shared_handle, __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(out_texture)))) {
      d3d11_device_ = std::move(device);
      d3d11_context_ = std::move(context);
      std::ostringstream stream;
      stream << "OsrRenderHandler::OpenSharedTexture matched adapter index="
             << index;
      DiagnosticLog(stream.str());
      return true;
    }
  }
  return false;
}

bool OsrRenderHandler::ReadSharedTexture(PaintElementType type,
                                         const RectList& dirtyRects,
                                         void* shared_handle,
                                         double scale) {
  (void)dirtyRects;
  if (!shared_handle || !frame_) {
    DiagnosticLog("OsrRenderHandler::ReadSharedTexture invalid input");
    return false;
  }
  if (!EnsureD3D11Device()) {
    return false;
  }

  Microsoft::WRL::ComPtr<ID3D11Texture2D> shared_texture;
  if (!OpenSharedTexture(shared_handle, shared_texture.GetAddressOf())) {
    std::ostringstream stream;
    stream << "OsrRenderHandler::ReadSharedTexture OpenSharedResource failed "
           << "shared_handle="
           << HexValue(reinterpret_cast<uintptr_t>(shared_handle));
    DiagnosticLog(stream.str());
    return false;
  }

  D3D11_TEXTURE2D_DESC desc = {};
  shared_texture->GetDesc(&desc);
  const int width = static_cast<int>(desc.Width);
  const int height = static_cast<int>(desc.Height);
  if (width <= 0 || height <= 0) {
    DiagnosticLog("OsrRenderHandler::ReadSharedTexture invalid texture desc");
    return false;
  }
  if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
    std::ostringstream stream;
    stream << "OsrRenderHandler::ReadSharedTexture unexpected format="
           << static_cast<int>(desc.Format);
    DiagnosticLog(stream.str());
    return false;
  }

  // 按尺寸缓存 staging 纹理（CPU 可读），尺寸变化时重建。
  if (!staging_texture_ || staging_width_ != width ||
      staging_height_ != height) {
    D3D11_TEXTURE2D_DESC staging_desc = {};
    staging_desc.Width = desc.Width;
    staging_desc.Height = desc.Height;
    staging_desc.MipLevels = 1;
    staging_desc.ArraySize = 1;
    staging_desc.Format = desc.Format;
    staging_desc.SampleDesc.Count = 1;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_texture_.Reset();
    const HRESULT hr = d3d11_device_->CreateTexture2D(
        &staging_desc, nullptr, staging_texture_.GetAddressOf());
    if (FAILED(hr)) {
      std::ostringstream stream;
      stream << "OsrRenderHandler::ReadSharedTexture CreateTexture2D staging "
             << "failed hr=0x" << std::hex << static_cast<uint32_t>(hr);
      DiagnosticLog(stream.str());
      return false;
    }
    staging_width_ = width;
    staging_height_ = height;
  }

  d3d11_context_->CopyResource(staging_texture_.Get(), shared_texture.Get());

  D3D11_MAPPED_SUBRESOURCE mapped = {};
  const HRESULT map_hr = d3d11_context_->Map(
      staging_texture_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  if (FAILED(map_hr)) {
    std::ostringstream stream;
    stream << "OsrRenderHandler::ReadSharedTexture Map failed hr=0x"
           << std::hex << static_cast<uint32_t>(map_hr);
    DiagnosticLog(stream.str());
    return false;
  }

  // staging 行距（RowPitch）可能大于 width*4，逐行拷贝为紧凑 BGRA buffer。
  const size_t row_bytes = static_cast<size_t>(width) * 4;
  std::vector<uint8_t> buffer(static_cast<size_t>(height) * row_bytes);
  const auto* src = static_cast<const uint8_t*>(mapped.pData);
  for (int y = 0; y < height; ++y) {
    std::memcpy(buffer.data() + static_cast<size_t>(y) * row_bytes,
                src + static_cast<size_t>(y) * mapped.RowPitch, row_bytes);
  }
  d3d11_context_->Unmap(staging_texture_.Get(), 0);

  if (render_stats_) {
    render_stats_->OnSetViewImageBegin();
  }
  if (type == PET_VIEW) {
    frame_->SetViewImage(buffer.data(), width, height, scale);
  } else if (type == PET_POPUP) {
    frame_->SetPopupImage(buffer.data(), width, height, scale);
  } else {
    return false;
  }
  if (render_stats_) {
    render_stats_->OnSetViewImageDone();
  }
  return true;
}

}  // namespace offscreen
