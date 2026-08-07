# Qt OpenGL CEF GPU Chain Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render CEF 100 OSR accelerated frames in a Qt 6.9.3 `QOpenGLWidget` through D3D11/WGL texture interop without CPU pixel readback on the successful GPU path.

**Architecture:** `OsrRenderHandler` copies each callback-scoped CEF D3D11 texture into an application-owned texture managed by `GpuFrameBridge`. `BrowserGlRenderer`, called only while the `QOpenGLWidget` context is current, registers that texture through `WGL_NV_DX_interop`, samples it in OpenGL, and falls back to CPU image upload when interop is unavailable.

**Tech Stack:** C++17, CEF 100.0.14, Qt 6.9.3 Widgets/OpenGL/OpenGLWidgets, D3D11, DXGI, WGL_NV_DX_interop, CMake 3.21+, VS2022 x64.

## Global Constraints

- Windows x64 only, with CEF `100.0.14+g4e5ba66+chromium-100.0.4896.75_windows64_minimal`, Qt 6.9.3 `msvc2022_64`, VS2022 and `/MD` or `/MDd`.
- A successful GPU frame must not use a staging texture, `Map`, `QImage`, or CPU pixel readback.
- Never retain the CEF shared handle beyond `OnAcceleratedPaint`; copy it immediately into an application-owned D3D11 texture.
- Preserve lifecycle, navigation, input, IME, drag/drop, popup, multi-tab APIs and latest-frame update coalescing.
- Preserve a visible CPU fallback when WGL interop or accelerated paint is unavailable.
- Real WGL/D3D driver validation is mandatory and must not be replaced by mocks.

---

## File Structure

- `src/browser/gpu_frame_state.*`: Qt/CEF-free generations and presentation-path decisions.
- `src/browser/gpu_frame_bridge.*`: D3D11 shared-handle opening, adapter selection and GPU-to-GPU copy.
- `src/qt/browser_gl_renderer.*`: GL shaders, CPU upload and WGL/D3D registration/drawing.
- Existing render handler/service/widget/statistics files: integration and telemetry.

### Task 1: GPU presentation state and telemetry

**Files:**
- Create: `src/browser/gpu_frame_state.h`
- Create: `src/browser/gpu_frame_state.cpp`
- Create: `tests/gpu_frame_state_tests.cpp`
- Modify: `src/browser/render_stats.h`
- Modify: `src/browser/render_stats.cpp`
- Modify: `src/browser/render_stats_log.cpp`
- Modify: `tests/render_stats_tests.cpp`
- Modify: `tests/render_stats_log_tests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `GpuFrameKind`, `GpuPresentPath`, `GpuPresentPathName`.
- Produces: `GpuFramePublicationState::Publish(GpuFrameKind, int, int, uint32_t)`.
- Produces: `RenderStats::OnGpuFramePresented(GpuPresentPath)` and GPU-path snapshot counters.

- [ ] **Step 1: Write failing publication-state tests**

```cpp
offscreen::GpuFramePublicationState state;
const auto first = state.Publish(offscreen::GpuFrameKind::kView, 1920, 1080, 87);
const auto second = state.Publish(offscreen::GpuFrameKind::kView, 1920, 1080, 87);
const auto resized = state.Publish(offscreen::GpuFrameKind::kView, 1280, 720, 87);
expect_eq(first.resource_generation, uint64_t{1}, "first resource");
expect_eq(second.resource_generation, uint64_t{1}, "same resource");
expect_eq(second.frame_generation, uint64_t{2}, "second frame");
expect_eq(resized.resource_generation, uint64_t{2}, "resized resource");
const auto popup = state.Publish(offscreen::GpuFrameKind::kPopup, 200, 100, 87);
expect_eq(popup.resource_generation, uint64_t{1}, "popup independent");
```

- [ ] **Step 2: Add `gpu_frame_state_tests` and verify RED**

Run `cmake --build build/cef100-msvc2022-x64 --config Release --target gpu_frame_state_tests`.

Expected: compile failure because `browser/gpu_frame_state.h` does not exist.

- [ ] **Step 3: Implement the state types**

```cpp
enum class GpuFrameKind : uint8_t { kView, kPopup };
enum class GpuPresentPath : uint8_t {
  kUnknown, kWglDxInterop, kCpuGlUpload, kQImageFallback
};
struct GpuFramePublication {
  uint64_t resource_generation = 0;
  uint64_t frame_generation = 0;
  int width = 0;
  int height = 0;
  uint32_t format = 0;
};
class GpuFramePublicationState {
 public:
  GpuFramePublication Publish(GpuFrameKind kind, int width, int height,
                              uint32_t format);
  GpuFramePublication Current(GpuFrameKind kind) const;
 private:
  GpuFramePublication view_;
  GpuFramePublication popup_;
};
```

`Publish` increments `resource_generation` only when dimensions or format change, and increments `frame_generation` for every valid publish.

- [ ] **Step 4: Verify state GREEN**

Run `ctest --test-dir build/cef100-msvc2022-x64 -C Release -R gpu_frame_state_tests --output-on-failure`.

Expected: PASS.

- [ ] **Step 5: Write failing RenderStats assertions**

```cpp
stats.OnGpuFramePresented(offscreen::GpuPresentPath::kWglDxInterop);
stats.OnGpuFramePresented(offscreen::GpuPresentPath::kWglDxInterop);
stats.OnGpuFramePresented(offscreen::GpuPresentPath::kQImageFallback);
const auto snap = stats.Snapshot();
expect_eq(snap.total_d3d_to_gl_frames, uint64_t{2}, "interop total");
expect_eq(snap.total_cpu_readback_fallback_frames, uint64_t{1}, "fallback total");
expect_eq(snap.gpu_present_path, std::string("qimage_fallback"), "last path");
```

Append CSV columns `gpu_present_path,d3d_to_gl_frames,cpu_readback_fallback_frames,total_d3d_to_gl_frames,total_cpu_readback_fallback_frames` and assert the exact header.

- [ ] **Step 6: Verify telemetry RED, implement, then verify GREEN**

First build `render_stats_tests render_stats_log_tests` and require compile failure. Then implement window/total counters, reset only window counters, and add `gpu_path=`, `d3d_gl=` and `cpu_fallback=` to `FormatRenderStatsSummary`.

Run `ctest --test-dir build/cef100-msvc2022-x64 -C Release -R "render_stats_tests|render_stats_log_tests" --output-on-failure`.

Expected: PASS.

- [ ] **Step 7: Commit Task 1**

```powershell
git add CMakeLists.txt tests/CMakeLists.txt tests/gpu_frame_state_tests.cpp tests/render_stats_tests.cpp tests/render_stats_log_tests.cpp src/browser/gpu_frame_state.h src/browser/gpu_frame_state.cpp src/browser/render_stats.h src/browser/render_stats.cpp src/browser/render_stats_log.cpp
git commit -m "feat: add GPU presentation telemetry"
```

### Task 2: D3D11 GPU frame bridge

**Files:**
- Create: `src/browser/gpu_frame_bridge.h`
- Create: `src/browser/gpu_frame_bridge.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/gpu_frame_bridge_tests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 state types.
- Produces: `GpuFrameBridge::CopyFromSharedHandle(GpuFrameKind, void*)` returning `GpuFrameCopyResult`.
- Produces: `GpuFrameBridge::Snapshot(GpuFrameKind)` returning a `GpuFrameSnapshot` that retains `ComPtr<ID3D11Device>` and `ComPtr<ID3D11Texture2D>`.

- [ ] **Step 1: Write failing empty-bridge test**

```cpp
#include "browser/gpu_frame_bridge.h"
void test_empty_bridge_snapshot() {
  offscreen::GpuFrameBridge bridge;
  const auto snapshot = bridge.Snapshot(offscreen::GpuFrameKind::kView);
  expect_true(!snapshot.texture, "empty bridge has no texture");
  expect_eq(snapshot.publication.frame_generation, uint64_t{0},
            "empty generation");
}
```

- [ ] **Step 2: Verify RED**

Add `gpu_frame_bridge_tests` from `tests/gpu_frame_bridge_tests.cpp` and `src/browser/gpu_frame_bridge.cpp`, linked to `offscreen_core`, `d3d11`, and `dxgi`. Run `cmake --build build/cef100-msvc2022-x64 --config Release --target gpu_frame_bridge_tests`.

Expected: compile failure because `gpu_frame_bridge.h` is absent.

- [ ] **Step 3: Implement shared texture opening and GPU copy**

```cpp
if (!shared_handle) return Failure("null_shared_handle");
ComPtr<ID3D11Texture2D> source;
if (!OpenSharedTexture(shared_handle, source.GetAddressOf()))
  return Failure("open_shared_texture_failed");
D3D11_TEXTURE2D_DESC source_desc{};
source->GetDesc(&source_desc);
EnsureDestination(kind, source_desc);
device_context_->CopyResource(destination.Get(), source.Get());
device_context_->Flush();
Publish(kind, source_desc.Width, source_desc.Height, source_desc.Format);
```

Destination textures use `D3D11_USAGE_DEFAULT`, `D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET`, one mip, one array slice, and no CPU access. Move existing matching-adapter enumeration from `OsrRenderHandler` into this class. Do not create a staging texture.

- [ ] **Step 4: Verify GREEN and widget-library linkage**

Run `cmake --build build/cef100-msvc2022-x64 --config Release --target gpu_frame_bridge_tests offscreen_cef_widgets` followed by `ctest --test-dir build/cef100-msvc2022-x64 -C Release -R gpu_frame_bridge_tests --output-on-failure`.

Expected: PASS and successful link against `d3d11`/`dxgi`.

- [ ] **Step 5: Commit Task 2**

```powershell
git add src/CMakeLists.txt tests/CMakeLists.txt tests/gpu_frame_bridge_tests.cpp src/browser/gpu_frame_bridge.h src/browser/gpu_frame_bridge.cpp
git commit -m "feat: copy CEF shared textures on GPU"
```

### Task 3: OpenGL renderer with WGL/D3D11 interop

**Files:**
- Create: `src/qt/browser_gl_renderer.h`
- Create: `src/qt/browser_gl_renderer.cpp`
- Modify: `src/browser/gpu_frame_state.h`
- Modify: `src/browser/gpu_frame_state.cpp`
- Modify: `tests/gpu_frame_state_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: bridge snapshots, CPU `BrowserFrameSnapshot`, popup metadata and `RenderStats`.
- Produces: `BrowserGlRenderer::Initialize`, `Render`, and `Shutdown`, all called with a current Qt GL context.

- [ ] **Step 1: Write failing path-selection tests**

```cpp
expect_eq(offscreen::ChooseGpuPresentPath(true, true, false),
          offscreen::GpuPresentPath::kWglDxInterop, "interop");
expect_eq(offscreen::ChooseGpuPresentPath(false, true, true),
          offscreen::GpuPresentPath::kCpuGlUpload, "CPU GL upload");
expect_eq(offscreen::ChooseGpuPresentPath(false, false, true),
          offscreen::GpuPresentPath::kQImageFallback, "QImage fallback");
```

The exact signature is `GpuPresentPath ChooseGpuPresentPath(bool interop_available, bool gl_upload_available, bool cpu_frame_available)`.

- [ ] **Step 2: Verify RED, implement selection, verify GREEN**

Build the test before and after implementing the exact signature. Require compile failure first and PASS after implementation.

- [ ] **Step 3: Implement renderer initialization**

Load `wglDXOpenDeviceNV`, `wglDXCloseDeviceNV`, `wglDXRegisterObjectNV`, `wglDXUnregisterObjectNV`, `wglDXSetResourceShareHandleNV`, `wglDXLockObjectsNV` and `wglDXUnlockObjectsNV` via `QOpenGLContext::getProcAddress`. Create view/popup GL textures, CPU-upload textures, a textured-quad shader and vertex data. Log `GL_VENDOR`, `GL_RENDERER`, `GL_VERSION` and extension availability.

- [ ] **Step 4: Implement registration, GPU drawing and CPU upload**

On changed `resource_generation`, unregister the old object, retain the new D3D texture, and register it as `GL_TEXTURE_2D` with `WGL_ACCESS_READ_ONLY_NV`. Every GPU draw must lock, draw, and unlock on every exit path. Draw popup after view. For CPU fallback use `glTexImage2D` on size changes and `glTexSubImage2D` otherwise.

- [ ] **Step 5: Update CMake and compile**

Require Qt `Widgets OpenGL OpenGLWidgets`; link `Qt6::OpenGL`, `Qt6::OpenGLWidgets` and `opengl32` into `offscreen_cef_widgets`.

Run `cmake --build build/cef100-msvc2022-x64 --config Release --target offscreen_cef_widgets`.

Expected: successful compile and link.

- [ ] **Step 6: Commit Task 3**

```powershell
git add CMakeLists.txt src/CMakeLists.txt src/browser/gpu_frame_state.h src/browser/gpu_frame_state.cpp tests/gpu_frame_state_tests.cpp src/qt/browser_gl_renderer.h src/qt/browser_gl_renderer.cpp
git commit -m "feat: add WGL D3D texture renderer"
```

### Task 4: Integrate accelerated frames into BrowserWidget

**Files:**
- Modify: `src/browser/osr_render_handler.h`
- Modify: `src/browser/osr_render_handler.cpp`
- Modify: `src/browser/browser_service.h`
- Modify: `src/browser/browser_service.cpp`
- Modify: `src/qt/browser_widget.h`
- Modify: `src/qt/browser_widget.cpp`
- Modify: `src/app/browser_app.cpp`

**Interfaces:**
- Produces: `BrowserService::gpu_frame_bridge()` and `BrowserWidget::SetGpuFrameBridge(std::shared_ptr<GpuFrameBridge>)`.
- Preserves all existing widget signals and event handlers.

- [ ] **Step 1: Convert BrowserWidget to QOpenGLWidget**

Replace `paintEvent` with `initializeGL`/`paintGL`. `paintGL` snapshots GPU view/popup resources and CPU fallback frame, then delegates to `BrowserGlRenderer`. Keep `resizeEvent` and all input handlers. The destructor calls `makeCurrent()`, renderer `Shutdown()`, and `doneCurrent()` when a context exists.

- [ ] **Step 2: Wire one bridge per BrowserService**

Construct `gpu_frame_bridge_` beside `frame_` and `render_stats_`, pass it into `OsrRenderHandler`, expose it, and bind it to the widget wherever `SetFrame` is currently called.

- [ ] **Step 3: Replace accelerated readback with bridge publication**

```cpp
const auto kind = type == PET_POPUP ? GpuFrameKind::kPopup
                                    : GpuFrameKind::kView;
const auto result = gpu_frame_bridge_->CopyFromSharedHandle(kind, shared_handle);
if (!result.success) {
  const bool read_ok = ReadSharedTexture(type, dirtyRects, shared_handle, scale);
  render_stats_->OnGpuFramePresented(GpuPresentPath::kQImageFallback);
  LogFallback(result.error, read_ok);
}
```

On success, do not call `ReadSharedTexture`, `SetViewImage`, or `SetPopupImage`. Record `kWglDxInterop` only after the GL draw succeeds.

- [ ] **Step 4: Pin CEF ANGLE to D3D11**

Add `AppendSwitchWithValue("use-angle", "d3d11")` and update the startup diagnostic. Do not add SwiftShader, WARP or GPU-disable switches.

- [ ] **Step 5: Build and run all tests**

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Architecture x64 -CefRoot "D:\Git\cef_binary_100.0.14+g4e5ba66+chromium-100.0.4896.75_windows64_minimal" -QtPrefix "D:\IDE\QT6.9.3\6.9.3\msvc2022_64" -Configuration Release
```

Expected: all targets and CTests pass; `Qt6OpenGL.dll` and `Qt6OpenGLWidgets.dll` are deployed.

- [ ] **Step 6: Commit Task 4**

```powershell
git add src/browser/osr_render_handler.h src/browser/osr_render_handler.cpp src/browser/browser_service.h src/browser/browser_service.cpp src/qt/browser_widget.h src/qt/browser_widget.cpp src/app/browser_app.cpp
git commit -m "feat: present CEF accelerated frames in Qt GL"
```

### Task 5: Runtime GPU-chain acceptance and fallback

**Files:**
- Verify: fresh rows in the four hourly debug/OSR/stats/lifecycle logs under `bin/msvc-2022-x64/Release`.
- Modify only observed failing Task 2-4 files when logs identify a concrete failure stage.

**Interfaces:**
- Produces evidence that the current machine uses `wgl_dx_interop`, or a concrete blocker/fallback reason.

- [ ] **Step 1: Record log boundaries and launch the test page**

```powershell
bin\msvc-2022-x64\Release\offscreen_cef_browser.exe --url=file:///D:/Git/offscreen-cef-browser/tests/visualization_compatibility_test.html
```

Exercise animation/video, resize and popup, then close normally.

- [ ] **Step 2: Verify fresh-run GPU evidence**

Require `OnAcceleratedPaint > 0`, increasing `d3d_to_gl_frames`, `gpu_present_path=wgl_dx_interop`, no fallback/readback for normal accelerated view frames, visible main/popup content, and lifecycle `shutdown_completed=1`/`exit_code=0`.

- [ ] **Step 3: Diagnose only the observed failure stage**

Use these exact boundaries: no callback means CEF GPU/ANGLE/shared-texture startup; `open_shared_texture_failed` means adapter/LUID; missing WGL functions means unsupported A-path driver; register failure means texture flags/format/context; lock/unlock failure means ownership lifecycle. Do not call CPU fallback a GPU success.

- [ ] **Step 4: Verify CPU fallback**

Run once with `--disable-gpu`; require a visible page, `cpu_gl_upload` or `qimage_fallback`, a nonzero fallback counter, and clean shutdown.

- [ ] **Step 5: Final regression checks**

```powershell
ctest --test-dir build/cef100-msvc2022-x64 -C Release --output-on-failure
git diff --check
git status --short
```

Expected: all tests pass, no whitespace errors, and only intentional files remain changed.

- [ ] **Step 6: Commit runtime-proven fixes only when present**

Stage only reviewed corrections and commit with `git commit -m "fix: complete Qt GL GPU presentation path"`; do not create an empty commit.
