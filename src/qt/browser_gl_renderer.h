#pragma once

#include <memory>
#include <string>

#include "browser/browser_frame.h"
#include "browser/gpu_frame_bridge.h"
#include "browser/gpu_frame_state.h"

namespace offscreen {

// 一次 GL 呈现的结果：主视图与弹出层各自的 GPU 尝试与失败阶段。
// failure_stage 取值与 GpuCopyPolicy 约定一致：""（成功）、"open_device"、
// "register"、"lock"、"unlock"；未尝试 GPU 时也为空串。
struct GpuPresentResult {
  GpuPresentPath path = GpuPresentPath::kUnknown;  // 主视图最终呈现路径（遥测用）
  // 主视图
  bool view_attempted_gpu = false;   // 有 GPU 帧且 interop 可用
  bool view_gpu_succeeded = false;   // 主视图 GPU 呈现成功
  std::string view_failure_stage;    // 主视图 GPU 失败阶段（成功为空）
  bool view_presented = false;       // 主视图已画出（GPU 或 CPU 上传）
  // 弹出层
  bool popup_attempted_gpu = false;
  bool popup_gpu_succeeded = false;
  std::string popup_failure_stage;
  bool popup_presented = false;
};

class BrowserGlRenderer {
 public:
  BrowserGlRenderer();
  ~BrowserGlRenderer();

  bool Initialize();
  GpuPresentResult Render(const GpuFrameSnapshot& view_gpu,
                          const GpuFrameSnapshot& popup_gpu,
                          const BrowserFrameSnapshot& cpu_frame,
                          int viewport_width,
                          int viewport_height);
  void Shutdown();

  bool interop_available() const;
  const std::string& last_error() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace offscreen
