# V2.2 Software OSR Paint Design

## Purpose

V2.2 turns the V2.1 windowless CEF browser into a visible software-rendered browser surface. The goal is a closed display loop: CEF `OnPaint` provides BGRA buffers, the application copies them into owned frame storage, and `BrowserWidget` paints them with `QPainter`.

This stage deliberately excludes mouse, wheel, keyboard, focus, and Chinese IME forwarding. Those belong to V2.3 so rendering, DPI, dirty rect, and popup issues can be validated independently.

## Target Stack

- Windows x64 only.
- Qt 5.14.2 MSVC2017 64bit.
- CEF 96 windows64 VS2017 binary distribution.
- CMake-generated Visual Studio 2017 x64 build.
- Software OSR only: CEF `OnPaint` BGRA buffer copied into application-owned storage and drawn by `QWidget`/`QPainter`.

## Current Baseline

V2.1 already provides:

- `BrowserWidget` as the native Qt surface.
- `BrowserService` browser lifecycle, navigation, close, and resize forwarding.
- `BrowserClient` lifecycle/load/display/request handlers.
- `MinimalRenderHandler` with `GetViewRect`, `GetScreenInfo`, and no-op `OnPaint`.
- Runtime smoke evidence that a windowless browser can be created, load `https://example.com/`, resize, close, and exit without subprocess leftovers.

V2.2 replaces the no-op paint path while preserving the V2.1 lifecycle behavior.

## In Scope

- Add frame storage for latest view and popup images.
- Copy `OnPaint(PET_VIEW)` and `OnPaint(PET_POPUP)` buffers immediately.
- Paint the latest view image in `BrowserWidget::paintEvent`.
- Composite popup image over the view when CEF reports an active popup rectangle.
- Convert dirty rects from physical pixels to Qt update regions in DIP.
- Track device scale factor from the Qt widget and report it through `GetScreenInfo`.
- Set `QImage::devicePixelRatio` so physical-pixel buffers display at the correct DIP size.
- Preserve V2.1 build, tests, browser creation, resize, close, and runtime smoke behavior.

## Out Of Scope

- Mouse, wheel, keyboard, focus, shortcut, and IME event forwarding.
- Address bar, navigation controls, DevTools UI, and crash-reload UX.
- Cursor shape updates.
- `QOpenGLWidget`, texture upload, shared texture, or `OnAcceleratedPaint`.
- Qt 6.9.3, win32/x86, linux-amd64, or macOS support.
- Broad public-web compatibility validation beyond rendering smoke checks.

## Architecture

V2.2 keeps the V2.1 module boundaries and changes the render path:

- `BrowserFrame`: owns copied view and popup image data plus dirty/update metadata.
- `OsrRenderHandler`: replaces `MinimalRenderHandler`; implements view rect, screen info, paint, and popup callbacks.
- `BrowserWidget`: paints the current `BrowserFrame` with `QPainter` and schedules updates from dirty regions.
- `BrowserService`: creates the render handler, wires update callbacks, forwards resize and scale changes, and keeps existing lifecycle behavior.

The render handler must never store CEF's raw `buffer` pointer after `OnPaint` returns. It copies the bytes needed for the current frame before returning to CEF.

## Data Flow

1. `BrowserWidget` provides the current DIP view rectangle and device scale factor.
2. `BrowserService` passes those values to `OsrRenderHandler` when creating or resizing the browser.
3. CEF calls `OsrRenderHandler::GetViewRect`; the returned rectangle is in DIP.
4. CEF calls `OsrRenderHandler::GetScreenInfo`; `device_scale_factor` matches the widget scale.
5. CEF calls `OnPaint` with BGRA data in physical pixels.
6. `OsrRenderHandler` copies the BGRA buffer into `BrowserFrame` and converts dirty rects to a Qt update region.
7. `BrowserWidget` receives a queued update request and repaints the dirty region on the GUI thread.
8. `BrowserWidget::paintEvent` draws the main view image first, then draws the popup image at the popup DIP rectangle if visible.

## DPI And Coordinates

- Qt widget dimensions and CEF `GetViewRect` use DIP.
- CEF `OnPaint` width, height, and dirty rects use physical pixels.
- `device_scale_factor` is read from the widget's current screen, falling back to `devicePixelRatioF()`.
- Copied `QImage` objects use `QImage::Format_ARGB32` for CEF BGRA data and set `devicePixelRatio(scale)`.
- Dirty rect conversion divides physical coordinates by `scale` and expands outward so fractional values do not miss pixels.
- Popup rectangles from CEF are treated as DIP rectangles for placement; popup image buffers still use physical pixels and the same image device scale rule.

## Threading

CEF callbacks may not be on the Qt GUI stack. V2.2 avoids direct painting from CEF callbacks:

- `OnPaint` copies data into application-owned frame storage.
- Any request that touches Qt widget repainting is delivered with a queued Qt call.
- Shared frame access is guarded with a small mutex or copy-swap boundary so Qt never reads partially written image data.
- The implementation should keep the lock scope short: copy or swap frame data under lock, draw outside long-running CEF callback work where possible.

## Error Handling

- Ignore paint callbacks with null buffers or non-positive width/height and keep the last valid frame.
- If the widget is resized to zero, continue using the existing one-pixel minimum view rect helper.
- If a popup is hidden, clear only the popup-visible flag and preserve the main view.
- If an unknown paint element type is received, ignore it without crashing.
- If the browser has not produced a frame yet, `BrowserWidget` paints a neutral background.

## Testing And Validation

Unit-level coverage should focus on Qt-free helpers where possible:

- Physical dirty rect to DIP rect conversion with scale `1.0`, `1.25`, `1.5`, and `2.0`.
- Clamping invalid scale values to `1.0` for safe conversion.
- Popup visibility state transitions in Qt-free frame metadata.

Build and CTest baseline:

```powershell
cmake --build "build\v1-cef96-msvc2017-x64" --config Debug --target offscreen_core_tests offscreen_cef_browser offscreen_cef_subprocess
ctest --test-dir "build\v1-cef96-msvc2017-x64" -C Debug --output-on-failure
```

Runtime validation:

```powershell
build\v1-cef96-msvc2017-x64\src\Debug\offscreen_cef_browser.exe --url=https://example.com/
```

Expected runtime results:

- The page becomes visible in the Qt window.
- Resizing the window resizes the rendered page without obvious stretching or stale borders.
- Animated or loading content repaints after the first frame.
- A popup surface, such as a native HTML select/dropdown on a local test page, is composited in the expected position if such a page is used for manual validation.
- Closing the app exits with no lingering `offscreen_cef_subprocess.exe` processes.

## Success Criteria

V2.2 is complete when the Windows x64 Debug build passes the existing CTest baseline, starts the V2.1 browser flow, visibly renders a loaded page through software OSR, handles resize and dirty-region repainting, composites popup paint buffers when present, preserves basic DPI sizing rules, and exits without leaving CEF subprocesses behind.
