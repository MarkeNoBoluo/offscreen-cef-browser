# V2 Windows x64 Functional Baseline Design

## Purpose

V2 turns the current Qt + CEF engineering skeleton into a usable off-screen browser on the already validated Windows x64 stack. The goal is to run real pages with visible OSR rendering, basic browser controls, input, Chinese IME, DevTools, and a deployable runtime directory before evaluating other platforms or Qt versions.

## Target Stack

- Windows x64 only.
- Qt 5.14.2 MSVC2017 64bit.
- CEF 96 windows64 VS2017 binary distribution unless a later explicit decision changes the V1/V2 stack.
- CMake-generated Visual Studio 2017 x64 build.
- Software OSR path: CEF `OnPaint` BGRA buffer copied into Qt and painted with `QWidget`/`QPainter`.

## Current Baseline

The repository already contains the V1 stage 1 skeleton:

- Top-level CMake project with Windows x64, VS2017, Qt 5.14.2, and CEF 96 guards.
- `offscreen_cef_browser` browser-process executable.
- `offscreen_cef_subprocess` CEF subprocess executable.
- `BrowserApp` with Alloy style switch and external message pump scheduling.
- `main.cpp` that initializes Qt and CEF, shows an empty `QMainWindow`, and shuts down CEF.
- `AppConfig` parsing for the initial URL.

V2 starts after that point. It does not revisit the already validated empty-window startup unless a later task breaks it.

## In Scope

V2 is a Windows x64 functional browser baseline with these stages:

1. Browser core creation.
2. Software OSR rendering.
3. Input, focus, resize, DPI, and Chinese IME.
4. Basic browser UX and DevTools.
5. Compatibility validation for local and target pages.
6. Windows x64 deployable runtime layout.

## Out Of Scope

- Qt 6.9.3 support.
- win32/x86 support.
- linux-amd64 support.
- macOS support.
- `QOpenGLWidget` texture-upload rendering.
- CEF shared texture / `OnAcceleratedPaint` rendering.
- Full product browser features unrelated to the core embedded-browser requirement.
- Large CEF or Qt binary distributions committed to the repository.

## Architecture

V2 keeps the two-process layout:

- `offscreen_cef_browser`: Qt GUI, CEF browser process, browser creation, painting, input, and UI controls.
- `offscreen_cef_subprocess`: CEF renderer/GPU/utility subprocess entry.

The browser process is split into focused modules:

- `BrowserService`: owns high-level browser lifecycle and exposes navigation, reload, stop, DevTools, and close operations.
- `BrowserClient`: aggregates CEF handlers and forwards lifecycle/load/display/render callbacks.
- `OsrRenderHandler`: implements view rect, screen info, popup, cursor, and paint callbacks.
- `BrowserFrame`: stores the latest view and popup images, dirty regions, DPI scale, and frame version.
- `BrowserWidget`: paints `BrowserFrame` and forwards Qt input events to CEF.
- `QtCefInputMapper`: converts Qt mouse, wheel, keyboard, focus, resize, and IME events into CEF host calls.

## Data Flow

Page load starts from `AppConfig::initial_url` or the address bar. `BrowserService` creates a windowless CEF browser using the `BrowserWidget` native handle and keeps the resulting `CefBrowser` after `OnAfterCreated`.

CEF paint callbacks copy BGRA data immediately into `BrowserFrame`. The Qt GUI thread receives an update request and draws the latest frame in `BrowserWidget::paintEvent`. Popup buffers are stored separately and composited over the main view at the CEF-provided popup rectangle.

Qt input events originate in `BrowserWidget`. Coordinates remain in DIP for CEF input APIs, while paint buffers are physical pixels. `GetScreenInfo().device_scale_factor`, `GetViewRect`, `QImage::setDevicePixelRatio`, and resize handling must stay consistent so high-DPI rendering and input hit testing line up.

## Lifecycle

Creation sequence:

1. Qt application and main window start as in V1 stage 1.
2. `BrowserWidget` is created as the central browser surface.
3. `BrowserService` creates `BrowserClient` and calls `CefBrowserHost::CreateBrowser` with `SetAsWindowless`.
4. `BrowserClient::OnAfterCreated` stores the `CefBrowser` reference and reports readiness to Qt.
5. The initial URL loads through the browser host.

Shutdown sequence:

1. Main window close requests browser shutdown through `BrowserService`.
2. `TryCloseBrowser` is used when a browser exists.
3. Qt close is delayed while CEF is still closing.
4. `OnBeforeClose` clears the browser reference and releases handlers.
5. After all browsers close, Qt exits and `CefShutdown()` runs.

## Error Handling

- CEF initialization failure remains a process exit failure.
- Browser creation failure shows an error state in the Qt window and does not crash the process.
- Paint callbacks with zero width or height are ignored and keep the last valid frame.
- Renderer crash is surfaced through the UI with a reload option.
- Missing deploy-time CEF or Qt runtime files fail validation rather than being hidden by developer-machine paths.
- DevTools remote debugging port remains opt-in and disabled by default for production-style runs.

## Testing And Validation

Each V2 stage must preserve the existing build and CTest baseline:

```powershell
cmake --build "build\v1-cef96-msvc2017-x64" --config Debug --target offscreen_core_tests offscreen_cef_browser offscreen_cef_subprocess
ctest --test-dir "build\v1-cef96-msvc2017-x64" -C Debug --output-on-failure
```

Manual/runtime validation expands by stage:

- Browser core: `OnAfterCreated` fires, `https://example.com/` loads, and resize reaches CEF.
- Rendering: first paint appears, animation/scroll repaint works, popup rendering is correctly placed.
- Input: click, text entry, keyboard shortcuts, wheel scrolling, focus changes, and Chinese IME composition/commit work.
- Browser UX: address navigation, back, forward, reload, stop, title/loading state, and DevTools work.
- Compatibility: local test pages and target visualization pages validate JavaScript, LocalStorage, Canvas, Video, Fetch, Cookie, WebSocket, Clipboard, Drag & Drop, and File API.
- Deployment: installed/copied runtime starts from a clean directory, loads a page, and exits without lingering `offscreen_cef_subprocess.exe` processes.

## V2 Stage Breakdown

### V2.1 Browser Core

Add `BrowserService`, `BrowserClient`, minimal lifespan/load/display handlers, and `BrowserWidget` ownership in the main window. This stage proves a windowless `CefBrowser` can be created and can load a URL, but does not require visible page rendering yet.

### V2.2 Software OSR Paint

Add `OsrRenderHandler` and `BrowserFrame`. Copy `OnPaint` buffers, draw with `QPainter`, support dirty rect updates, popup composition, cursor updates where practical, and DPI-aware frame sizing.

### V2.3 Input And IME

Forward mouse, wheel, keyboard, focus, resize, and Qt input method events to CEF. Validate Chinese pinyin input, candidate position behavior, text commit, selection updates, and high-DPI click alignment.

### V2.4 Browser UX

Replace the empty window with a minimal browser shell: address bar, navigation controls, loading/title status, error state, reload after crash, and a DevTools entry point.

### V2.5 Compatibility Validation

Add local compatibility pages under `resources/` and document target-page validation results. Focus on the browser capabilities required by the visualization system, not on broad public-web browser parity.

### V2.6 Windows x64 Deploy

Harden CMake install/copy rules for the selected Windows x64 stack. Verify the deploy directory contains the app executable, subprocess executable, CEF runtime/resources/locales, Qt platform plugin, required Qt DLLs, and no developer-machine absolute path dependency.

## Success Criteria

V2 is complete when the Windows x64 build can be installed or copied to a clean runtime directory, start the Qt browser shell, load the default and target pages, render through software OSR, accept mouse/keyboard/wheel/Chinese IME input, open DevTools, pass compatibility checks, and exit without lingering CEF subprocesses.
