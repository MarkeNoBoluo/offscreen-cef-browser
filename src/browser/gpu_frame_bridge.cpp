#include "browser/gpu_frame_bridge.h"

#include <cstdint>
#include <sstream>
#include <utility>

#include <dxgi.h>

#include "app/diagnostic_log.h"

namespace offscreen {

namespace {

constexpr D3D_FEATURE_LEVEL kFeatureLevels[] = {
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
};

GpuFrameCopyResult Failure(const std::string& error) {
  GpuFrameCopyResult result;
  result.error = error;
  return result;
}

}  // namespace

GpuFrameCopyResult GpuFrameBridge::CopyFromSharedHandle(
    GpuFrameKind kind,
    void* shared_handle) {
  if (!shared_handle) {
    return Failure("null_shared_handle");
  }

  std::lock_guard<std::mutex> lock(*access_mutex_);
  if (!EnsureD3D11Device()) {
    return Failure("create_d3d11_device_failed");
  }

  Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
  if (!OpenSharedTexture(shared_handle, source.GetAddressOf())) {
    return Failure("open_shared_texture_failed");
  }

  D3D11_TEXTURE2D_DESC source_desc{};
  source->GetDesc(&source_desc);
  if (source_desc.Width == 0 || source_desc.Height == 0) {
    return Failure("invalid_shared_texture_size");
  }

  std::string error;
  if (!EnsureDestination(kind, source_desc, &error)) {
    return Failure(error);
  }

  FrameSlot& slot = SelectSlot(kind);
  context_->CopyResource(slot.texture.Get(), source.Get());
  context_->Flush();

  GpuFrameCopyResult result;
  result.success = true;
  result.publication = publication_state_.Publish(
      kind, static_cast<int>(source_desc.Width),
      static_cast<int>(source_desc.Height),
      static_cast<uint32_t>(source_desc.Format));
  return result;
}

GpuFrameSnapshot GpuFrameBridge::Snapshot(GpuFrameKind kind) const {
  std::lock_guard<std::mutex> lock(*access_mutex_);
  GpuFrameSnapshot snapshot;
  snapshot.device = device_;
  snapshot.texture = SelectSlot(kind).texture;
  snapshot.publication = publication_state_.Current(kind);
  snapshot.access_mutex = access_mutex_;
  return snapshot;
}

void GpuFrameBridge::SetInteropAvailable(bool available) {
  std::lock_guard<std::mutex> lock(*access_mutex_);
  interop_available_ = available;
}

bool GpuFrameBridge::interop_available() const {
  std::lock_guard<std::mutex> lock(*access_mutex_);
  return interop_available_;
}

bool GpuFrameBridge::EnsureD3D11Device() {
  if (device_) {
    return true;
  }
  return SUCCEEDED(D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, kFeatureLevels,
      static_cast<UINT>(sizeof(kFeatureLevels) / sizeof(kFeatureLevels[0])),
      D3D11_SDK_VERSION, device_.GetAddressOf(), nullptr,
      context_.GetAddressOf()));
}

bool GpuFrameBridge::OpenSharedTexture(void* shared_handle,
                                       ID3D11Texture2D** out_texture) {
  if (!out_texture || !device_) {
    return false;
  }
  *out_texture = nullptr;
  if (SUCCEEDED(device_->OpenSharedResource(
          shared_handle, __uuidof(ID3D11Texture2D),
          reinterpret_cast<void**>(out_texture)))) {
    return true;
  }

  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(
          __uuidof(IDXGIFactory1),
          reinterpret_cast<void**>(factory.GetAddressOf())))) {
    return false;
  }

  for (UINT index = 0;; ++index) {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(index, adapter.GetAddressOf()) ==
        DXGI_ERROR_NOT_FOUND) {
      break;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> candidate_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> candidate_context;
    if (FAILED(D3D11CreateDevice(
            adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, kFeatureLevels,
            static_cast<UINT>(sizeof(kFeatureLevels) /
                              sizeof(kFeatureLevels[0])),
            D3D11_SDK_VERSION, candidate_device.GetAddressOf(), nullptr,
            candidate_context.GetAddressOf()))) {
      continue;
    }

    if (SUCCEEDED(candidate_device->OpenSharedResource(
            shared_handle, __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(out_texture)))) {
      device_ = std::move(candidate_device);
      context_ = std::move(candidate_context);
      view_.texture.Reset();
      popup_.texture.Reset();
      DiagnosticLog("GpuFrameBridge matched D3D11 adapter index=" +
                    std::to_string(index));
      return true;
    }
  }
  return false;
}

bool GpuFrameBridge::EnsureDestination(
    GpuFrameKind kind,
    const D3D11_TEXTURE2D_DESC& source_desc,
    std::string* error) {
  FrameSlot& slot = SelectSlot(kind);
  if (slot.texture) {
    D3D11_TEXTURE2D_DESC current{};
    slot.texture->GetDesc(&current);
    if (current.Width == source_desc.Width &&
        current.Height == source_desc.Height &&
        current.Format == source_desc.Format &&
        current.SampleDesc.Count == source_desc.SampleDesc.Count &&
        current.SampleDesc.Quality == source_desc.SampleDesc.Quality) {
      return true;
    }
    slot.texture.Reset();
  }

  D3D11_TEXTURE2D_DESC destination_desc{};
  destination_desc.Width = source_desc.Width;
  destination_desc.Height = source_desc.Height;
  destination_desc.MipLevels = 1;
  destination_desc.ArraySize = 1;
  destination_desc.Format = source_desc.Format;
  destination_desc.SampleDesc = source_desc.SampleDesc;
  destination_desc.Usage = D3D11_USAGE_DEFAULT;
  destination_desc.BindFlags =
      D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  destination_desc.CPUAccessFlags = 0;
  destination_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

  const HRESULT hr = device_->CreateTexture2D(
      &destination_desc, nullptr, slot.texture.GetAddressOf());
  if (FAILED(hr)) {
    std::ostringstream stream;
    stream << "create_destination_texture_failed:0x" << std::hex
           << static_cast<uint32_t>(hr);
    *error = stream.str();
    return false;
  }
  return true;
}

GpuFrameBridge::FrameSlot& GpuFrameBridge::SelectSlot(GpuFrameKind kind) {
  return kind == GpuFrameKind::kPopup ? popup_ : view_;
}

const GpuFrameBridge::FrameSlot& GpuFrameBridge::SelectSlot(
    GpuFrameKind kind) const {
  return kind == GpuFrameKind::kPopup ? popup_ : view_;
}

}  // namespace offscreen
