#include "browser/gpu_frame_state.h"

namespace offscreen {

const char* GpuPresentPathName(GpuPresentPath path) {
  switch (path) {
    case GpuPresentPath::kWglDxInterop:
      return "wgl_dx_interop";
    case GpuPresentPath::kCpuGlUpload:
      return "cpu_gl_upload";
    case GpuPresentPath::kQImageFallback:
      return "qimage_fallback";
    case GpuPresentPath::kUnknown:
      return "unknown";
  }
  return "unknown";
}

GpuPresentPath ChooseGpuPresentPath(bool wgl_dx_interop_available,
                                    bool open_gl_available,
                                    bool qimage_available) {
  if (wgl_dx_interop_available) {
    return GpuPresentPath::kWglDxInterop;
  }
  if (open_gl_available) {
    return GpuPresentPath::kCpuGlUpload;
  }
  if (qimage_available) {
    return GpuPresentPath::kQImageFallback;
  }
  return GpuPresentPath::kUnknown;
}

GpuFramePublication GpuFramePublicationState::Publish(GpuFrameKind kind,
                                                       int width,
                                                       int height,
                                                       uint32_t format) {
  GpuFramePublication& publication = Select(kind);
  if (publication.width != width || publication.height != height ||
      publication.format != format) {
    ++publication.resource_generation;
    publication.width = width;
    publication.height = height;
    publication.format = format;
  }
  ++publication.frame_generation;
  return publication;
}

GpuFramePublication GpuFramePublicationState::Current(
    GpuFrameKind kind) const {
  return Select(kind);
}

GpuFramePublication& GpuFramePublicationState::Select(GpuFrameKind kind) {
  return kind == GpuFrameKind::kPopup ? popup_ : view_;
}

const GpuFramePublication& GpuFramePublicationState::Select(
    GpuFrameKind kind) const {
  return kind == GpuFrameKind::kPopup ? popup_ : view_;
}

}  // namespace offscreen
