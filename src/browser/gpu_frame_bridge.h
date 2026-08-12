#pragma once

#include <memory>
#include <mutex>
#include <string>

#include <d3d11.h>
#include <wrl/client.h>

#include "browser/gpu_copy_policy.h"
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
  /// 绑定降级策略：复制前按资源查询是否允许，失败时按阶段上报。
  /// @param policy 共享的 GpuCopyPolicy；nullptr 表示不降级。
  void SetCopyPolicy(std::shared_ptr<GpuCopyPolicy> policy);
  /// 返回当前 D3D11 设备的 DXGI 适配器描述（UTF-8）；无设备时为空串。
  /// @return 适配器描述，如 "NVIDIA GeForce RTX 4060"。
  std::string adapter_description() const;

 private:
  // 每个资源双缓冲：textures[current] 为呈现面，复制写入后备再翻转。
  struct FrameSlot {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> textures[2];
    int current = 0;
  };

  bool EnsureD3D11Device();
  bool OpenSharedTexture(void* shared_handle,
                         ID3D11Texture2D** out_texture);
  bool EnsureDestination(GpuFrameKind kind, int buffer_index,
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
  std::shared_ptr<GpuCopyPolicy> copy_policy_;
  bool interop_available_ = false;
};

}  // namespace offscreen
