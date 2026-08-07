#include "qt/browser_gl_renderer.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <utility>

#include <Windows.h>

#include <QImage>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QVector4D>

#include "app/diagnostic_log.h"

namespace offscreen {

namespace {

constexpr GLenum kGlBgra = 0x80E1;
constexpr GLenum kWglAccessReadOnlyNv = 0x0000;

using WglDxOpenDevice = HANDLE(WINAPI*)(void*);
using WglDxCloseDevice = BOOL(WINAPI*)(HANDLE);
using WglDxRegisterObject = HANDLE(WINAPI*)(HANDLE, void*, GLuint, GLenum,
                                            GLenum);
using WglDxUnregisterObject = BOOL(WINAPI*)(HANDLE, HANDLE);
using WglDxSetResourceShareHandle = BOOL(WINAPI*)(void*, HANDLE);
using WglDxLockObjects = BOOL(WINAPI*)(HANDLE, GLint, HANDLE*);
using WglDxUnlockObjects = BOOL(WINAPI*)(HANDLE, GLint, HANDLE*);

template <typename Function>
Function ResolveWglFunction(QOpenGLContext* context, const char* name) {
  return reinterpret_cast<Function>(context->getProcAddress(name));
}

std::string GlString(QOpenGLFunctions* gl, GLenum name) {
  const GLubyte* value = gl->glGetString(name);
  return value ? reinterpret_cast<const char*>(value) : "unavailable";
}

}  // namespace

struct BrowserGlRenderer::Impl {
  struct TextureSlot {
    GLuint texture = 0;
    HANDLE interop_object = nullptr;
    uint64_t resource_generation = 0;
    int uploaded_width = 0;
    int uploaded_height = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> d3d_texture;
  };

  QOpenGLFunctions* gl = nullptr;
  std::unique_ptr<QOpenGLShaderProgram> shader;
  QOpenGLBuffer quad_buffer{QOpenGLBuffer::VertexBuffer};
  TextureSlot view_gpu;
  TextureSlot popup_gpu;
  TextureSlot view_cpu;
  TextureSlot popup_cpu;

  WglDxOpenDevice dx_open_device = nullptr;
  WglDxCloseDevice dx_close_device = nullptr;
  WglDxRegisterObject dx_register_object = nullptr;
  WglDxUnregisterObject dx_unregister_object = nullptr;
  WglDxSetResourceShareHandle dx_set_share_handle = nullptr;
  WglDxLockObjects dx_lock_objects = nullptr;
  WglDxUnlockObjects dx_unlock_objects = nullptr;
  HANDLE interop_device = nullptr;
  Microsoft::WRL::ComPtr<ID3D11Device> d3d_device;

  bool initialized = false;
  bool interop_supported = false;
  std::string error;

  void SetError(std::string value) {
    if (error != value) {
      error = std::move(value);
      DiagnosticLog("BrowserGlRenderer error=" + error);
    }
  }

  void CreateTexture(TextureSlot* slot) {
    gl->glGenTextures(1, &slot->texture);
    gl->glBindTexture(GL_TEXTURE_2D, slot->texture);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  void Unregister(TextureSlot* slot) {
    if (slot->interop_object && interop_device && dx_unregister_object) {
      dx_unregister_object(interop_device, slot->interop_object);
    }
    slot->interop_object = nullptr;
    slot->resource_generation = 0;
    slot->d3d_texture.Reset();
  }

  void CloseInteropDevice() {
    Unregister(&view_gpu);
    Unregister(&popup_gpu);
    if (interop_device && dx_close_device) {
      dx_close_device(interop_device);
    }
    interop_device = nullptr;
    d3d_device.Reset();
  }

  bool EnsureInteropDevice(ID3D11Device* device) {
    if (!interop_supported || !device) {
      return false;
    }
    if (interop_device && d3d_device.Get() == device) {
      return true;
    }
    CloseInteropDevice();
    interop_device = dx_open_device(device);
    if (!interop_device) {
      SetError("wglDXOpenDeviceNV_failed");
      return false;
    }
    d3d_device = device;
    return true;
  }

  bool EnsureRegistered(TextureSlot* slot,
                        const GpuFrameSnapshot& snapshot) {
    if (!snapshot.texture || snapshot.publication.resource_generation == 0 ||
        !EnsureInteropDevice(snapshot.device.Get())) {
      return false;
    }
    if (slot->interop_object &&
        slot->resource_generation ==
            snapshot.publication.resource_generation &&
        slot->d3d_texture.Get() == snapshot.texture.Get()) {
      return true;
    }

    Unregister(slot);
    slot->interop_object = dx_register_object(
        interop_device, snapshot.texture.Get(), slot->texture, GL_TEXTURE_2D,
        kWglAccessReadOnlyNv);
    if (!slot->interop_object) {
      SetError("wglDXRegisterObjectNV_failed");
      return false;
    }
    slot->resource_generation = snapshot.publication.resource_generation;
    slot->d3d_texture = snapshot.texture;
    DiagnosticLog(
        "BrowserGlRenderer registered D3D11 texture generation=" +
        std::to_string(slot->resource_generation) + " size=" +
        std::to_string(snapshot.publication.width) + "x" +
        std::to_string(snapshot.publication.height));
    return true;
  }

  bool DrawTexture(GLuint texture,
                   int x,
                   int y,
                   int width,
                   int height,
                   int viewport_width,
                   int viewport_height) {
    if (!texture || width <= 0 || height <= 0 || viewport_width <= 0 ||
        viewport_height <= 0) {
      return false;
    }

    const float left = -1.0f + 2.0f * static_cast<float>(x) /
                                   static_cast<float>(viewport_width);
    const float top = 1.0f - 2.0f * static_cast<float>(y) /
                                 static_cast<float>(viewport_height);
    const float span_x = 2.0f * static_cast<float>(width) /
                         static_cast<float>(viewport_width);
    const float span_y = -2.0f * static_cast<float>(height) /
                         static_cast<float>(viewport_height);

    shader->bind();
    shader->setUniformValue("uTexture", 0);
    shader->setUniformValue("uRect", QVector4D(left, top, span_x, span_y));
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, texture);
    quad_buffer.bind();
    const int position = shader->attributeLocation("aPosition");
    shader->enableAttributeArray(position);
    shader->setAttributeBuffer(position, GL_FLOAT, 0, 2, 0);
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    shader->disableAttributeArray(position);
    quad_buffer.release();
    shader->release();
    return true;
  }

  bool DrawGpu(TextureSlot* slot,
               const GpuFrameSnapshot& snapshot,
               int x,
               int y,
               int width,
               int height,
               int viewport_width,
               int viewport_height) {
    if (!EnsureRegistered(slot, snapshot)) {
      return false;
    }
    HANDLE object = slot->interop_object;
    if (!dx_lock_objects(interop_device, 1, &object)) {
      SetError("wglDXLockObjectsNV_failed");
      return false;
    }
    const bool drawn = DrawTexture(slot->texture, x, y, width, height,
                                   viewport_width, viewport_height);
    if (!dx_unlock_objects(interop_device, 1, &object)) {
      SetError("wglDXUnlockObjectsNV_failed");
      return false;
    }
    return drawn;
  }

  bool UploadAndDraw(TextureSlot* slot,
                     const QImage& image,
                     int x,
                     int y,
                     int width,
                     int height,
                     int viewport_width,
                     int viewport_height) {
    if (image.isNull()) {
      return false;
    }
    const QImage bgra = image.format() == QImage::Format_ARGB32
                            ? image
                            : image.convertToFormat(QImage::Format_ARGB32);
    gl->glBindTexture(GL_TEXTURE_2D, slot->texture);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (slot->uploaded_width != bgra.width() ||
        slot->uploaded_height != bgra.height()) {
      gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bgra.width(), bgra.height(),
                       0, kGlBgra, GL_UNSIGNED_BYTE, bgra.constBits());
      slot->uploaded_width = bgra.width();
      slot->uploaded_height = bgra.height();
    } else {
      gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, bgra.width(), bgra.height(),
                          kGlBgra, GL_UNSIGNED_BYTE, bgra.constBits());
    }
    return DrawTexture(slot->texture, x, y, width, height, viewport_width,
                       viewport_height);
  }
};

BrowserGlRenderer::BrowserGlRenderer() : impl_(std::make_unique<Impl>()) {}

BrowserGlRenderer::~BrowserGlRenderer() = default;

bool BrowserGlRenderer::Initialize() {
  QOpenGLContext* context = QOpenGLContext::currentContext();
  if (!context) {
    impl_->SetError("no_current_opengl_context");
    return false;
  }
  impl_->gl = context->functions();
  if (!impl_->gl) {
    impl_->SetError("initialize_opengl_functions_failed");
    return false;
  }
  impl_->gl->initializeOpenGLFunctions();

  impl_->shader = std::make_unique<QOpenGLShaderProgram>();
  static constexpr char kVertexShader[] = R"(
    attribute vec2 aPosition;
    uniform vec4 uRect;
    varying vec2 vTexCoord;
    void main() {
      gl_Position = vec4(uRect.xy + aPosition * uRect.zw, 0.0, 1.0);
      vTexCoord = aPosition;
    }
  )";
  static constexpr char kFragmentShader[] = R"(
    uniform sampler2D uTexture;
    varying vec2 vTexCoord;
    void main() {
      gl_FragColor = texture2D(uTexture, vTexCoord);
    }
  )";
  if (!impl_->shader->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                               kVertexShader) ||
      !impl_->shader->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                               kFragmentShader) ||
      !impl_->shader->link()) {
    impl_->SetError("shader_initialization_failed:" +
                    impl_->shader->log().toStdString());
    return false;
  }

  static constexpr std::array<float, 8> kQuad = {
      0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
  if (!impl_->quad_buffer.create() || !impl_->quad_buffer.bind()) {
    impl_->SetError("quad_buffer_initialization_failed");
    return false;
  }
  impl_->quad_buffer.allocate(kQuad.data(),
                              static_cast<int>(sizeof(kQuad)));
  impl_->quad_buffer.release();

  impl_->CreateTexture(&impl_->view_gpu);
  impl_->CreateTexture(&impl_->popup_gpu);
  impl_->CreateTexture(&impl_->view_cpu);
  impl_->CreateTexture(&impl_->popup_cpu);

  impl_->dx_open_device =
      ResolveWglFunction<WglDxOpenDevice>(context, "wglDXOpenDeviceNV");
  impl_->dx_close_device =
      ResolveWglFunction<WglDxCloseDevice>(context, "wglDXCloseDeviceNV");
  impl_->dx_register_object = ResolveWglFunction<WglDxRegisterObject>(
      context, "wglDXRegisterObjectNV");
  impl_->dx_unregister_object = ResolveWglFunction<WglDxUnregisterObject>(
      context, "wglDXUnregisterObjectNV");
  impl_->dx_set_share_handle = ResolveWglFunction<WglDxSetResourceShareHandle>(
      context, "wglDXSetResourceShareHandleNV");
  impl_->dx_lock_objects = ResolveWglFunction<WglDxLockObjects>(
      context, "wglDXLockObjectsNV");
  impl_->dx_unlock_objects = ResolveWglFunction<WglDxUnlockObjects>(
      context, "wglDXUnlockObjectsNV");
  impl_->interop_supported =
      impl_->dx_open_device && impl_->dx_close_device &&
      impl_->dx_register_object && impl_->dx_unregister_object &&
      impl_->dx_set_share_handle && impl_->dx_lock_objects &&
      impl_->dx_unlock_objects;

  impl_->initialized = true;
  DiagnosticLog("BrowserGlRenderer initialized vendor=" +
                GlString(impl_->gl, GL_VENDOR) + " renderer=" +
                GlString(impl_->gl, GL_RENDERER) + " version=" +
                GlString(impl_->gl, GL_VERSION) + " wgl_dx_interop=" +
                (impl_->interop_supported ? "available" : "unavailable"));
  return true;
}

GpuPresentPath BrowserGlRenderer::Render(
    const GpuFrameSnapshot& view_gpu,
    const GpuFrameSnapshot& popup_gpu,
    const BrowserFrameSnapshot& cpu_frame,
    int viewport_width,
    int viewport_height) {
  if (!impl_->initialized || !impl_->gl) {
    return GpuPresentPath::kUnknown;
  }
  impl_->gl->glViewport(0, 0, viewport_width, viewport_height);
  impl_->gl->glDisable(GL_DEPTH_TEST);
  impl_->gl->glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
  impl_->gl->glClear(GL_COLOR_BUFFER_BIT);

  bool drew_view = false;
  GpuPresentPath path = GpuPresentPath::kUnknown;
  if (impl_->interop_supported && view_gpu.texture) {
    std::unique_lock<std::mutex> gpu_access;
    if (view_gpu.access_mutex) {
      gpu_access = std::unique_lock<std::mutex>(*view_gpu.access_mutex);
    }
    drew_view = impl_->DrawGpu(&impl_->view_gpu, view_gpu, 0, 0,
                               viewport_width, viewport_height,
                               viewport_width, viewport_height);
    if (drew_view) {
      path = GpuPresentPath::kWglDxInterop;
    }
  }
  if (!drew_view && cpu_frame.has_view) {
    drew_view = impl_->UploadAndDraw(
        &impl_->view_cpu, cpu_frame.view_image, 0, 0, viewport_width,
        viewport_height, viewport_width, viewport_height);
    if (drew_view) {
      path = GpuPresentPath::kCpuGlUpload;
    }
  }

  if (cpu_frame.popup_visible) {
    const BrowserViewRect& rect = cpu_frame.popup_rect;
    bool popup_drawn = false;
    if (path == GpuPresentPath::kWglDxInterop && popup_gpu.texture) {
      std::unique_lock<std::mutex> gpu_access;
      if (popup_gpu.access_mutex) {
        gpu_access = std::unique_lock<std::mutex>(*popup_gpu.access_mutex);
      }
      popup_drawn = impl_->DrawGpu(
          &impl_->popup_gpu, popup_gpu, rect.x, rect.y, rect.width,
          rect.height, viewport_width, viewport_height);
    }
    if (!popup_drawn && !cpu_frame.popup_image.isNull()) {
      impl_->UploadAndDraw(&impl_->popup_cpu, cpu_frame.popup_image, rect.x,
                           rect.y, rect.width, rect.height, viewport_width,
                           viewport_height);
    }
  }
  return path;
}

void BrowserGlRenderer::Shutdown() {
  if (!impl_->initialized || !impl_->gl) {
    return;
  }
  impl_->CloseInteropDevice();
  for (Impl::TextureSlot* slot : {&impl_->view_gpu, &impl_->popup_gpu,
                                  &impl_->view_cpu, &impl_->popup_cpu}) {
    if (slot->texture) {
      impl_->gl->glDeleteTextures(1, &slot->texture);
      slot->texture = 0;
    }
  }
  if (impl_->quad_buffer.isCreated()) {
    impl_->quad_buffer.destroy();
  }
  impl_->shader.reset();
  impl_->initialized = false;
}

bool BrowserGlRenderer::interop_available() const {
  return impl_->interop_supported;
}

const std::string& BrowserGlRenderer::last_error() const {
  return impl_->error;
}

}  // namespace offscreen
