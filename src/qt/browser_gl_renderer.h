#pragma once

#include <memory>
#include <string>

#include "browser/browser_frame.h"
#include "browser/gpu_frame_bridge.h"
#include "browser/gpu_frame_state.h"

namespace offscreen {

class BrowserGlRenderer {
 public:
  BrowserGlRenderer();
  ~BrowserGlRenderer();

  bool Initialize();
  GpuPresentPath Render(const GpuFrameSnapshot& view_gpu,
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
