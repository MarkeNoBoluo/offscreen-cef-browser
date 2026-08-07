#pragma once

#include <memory>
#include <mutex>
#include <string>

#include <d3d11.h>
#include <wrl/client.h>

#include "browser/gpu_frame_state.h"

namespace offscreen {

struct GpuFrameCopyResult {
  bool success = false;
  std::string error;
  GpuFramePublication publication;
};

struct GpuFrameSnapshot {
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  GpuFramePublication publication;
  std::shared_ptr<std::mutex> access_mutex;
};

class GpuFrameBridge {
 public:
  GpuFrameBridge() = default;

  GpuFrameCopyResult CopyFromSharedHandle(GpuFrameKind kind,
                                          void* shared_handle);
  GpuFrameSnapshot Snapshot(GpuFrameKind kind) const;
  void SetInteropAvailable(bool available);
  bool interop_available() const;

 private:
  struct FrameSlot {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  };

  bool EnsureD3D11Device();
  bool OpenSharedTexture(void* shared_handle,
                         ID3D11Texture2D** out_texture);
  bool EnsureDestination(GpuFrameKind kind,
                         const D3D11_TEXTURE2D_DESC& source_desc,
                         std::string* error);
  FrameSlot& SelectSlot(GpuFrameKind kind);
  const FrameSlot& SelectSlot(GpuFrameKind kind) const;

  std::shared_ptr<std::mutex> access_mutex_ = std::make_shared<std::mutex>();
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  FrameSlot view_;
  FrameSlot popup_;
  GpuFramePublicationState publication_state_;
  bool interop_available_ = false;
};

}  // namespace offscreen
