#pragma once

#include <cstdint>

namespace offscreen {

enum class GpuFrameKind : uint8_t {
  kView = 0,
  kPopup,
};

enum class GpuPresentPath : uint8_t {
  kUnknown = 0,
  kWglDxInterop,
  kCpuGlUpload,
  kQImageFallback,
};

const char* GpuPresentPathName(GpuPresentPath path);

struct GpuFramePublication {
  uint64_t resource_generation = 0;
  uint64_t frame_generation = 0;
  int width = 0;
  int height = 0;
  uint32_t format = 0;
};

class GpuFramePublicationState {
 public:
  GpuFramePublication Publish(GpuFrameKind kind,
                              int width,
                              int height,
                              uint32_t format);
  GpuFramePublication Current(GpuFrameKind kind) const;

 private:
  GpuFramePublication& Select(GpuFrameKind kind);
  const GpuFramePublication& Select(GpuFrameKind kind) const;

  GpuFramePublication view_;
  GpuFramePublication popup_;
};

}  // namespace offscreen
