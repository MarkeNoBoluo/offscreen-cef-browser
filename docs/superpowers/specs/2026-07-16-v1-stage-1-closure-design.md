# V1 Stage 1 Closure Design

## Scope

Close V1 stage 1 by proving the existing Qt + CEF engineering skeleton builds, starts, and exits with the selected V1 runtime stack.

## Target Stack

- Windows x64.
- Qt 5.14.2 MSVC2017 64bit.
- CEF binary distribution: `D:\Git\cef_binary_96.0.18+gfe551e4+chromium-96.0.4664.110_windows64_vs2017`.
- CMake-generated VS2017 x64 build directory dedicated to this validation.

## In Scope

- Configure CMake with the provided `CEF_ROOT`.
- Build `offscreen_core`, `offscreen_core_tests`, `offscreen_cef_browser`, and `offscreen_cef_subprocess`.
- Keep the current two-process CEF layout.
- Preserve the current external CEF message pump integration with Qt.
- Fix only compile, link, runtime path, or CEF 96 compatibility issues found during validation.
- Verify that the app shows an empty Qt main window and exits cleanly.
- Verify CEF writes `cef.log` without fatal initialization errors.

## Out of Scope

- Creating a `CefBrowser` instance.
- Implementing `BrowserService`, `BrowserClient`, `CefRenderHandler`, painting, input, DPI, IME, DevTools, downloads, or browser navigation.
- General CMake restructuring not required for the selected stack.
- V2 stacks such as Qt 6, win32, or Linux.

## Validation

- CMake configure succeeds with `CEF_ROOT` set to the CEF 96 VS2017 x64 distribution.
- Debug build succeeds for the core test and both executable targets.
- `ctest --test-dir <build-dir> --output-on-failure` passes.
- Launching `offscreen_cef_browser.exe` opens a Qt main window.
- Closing the window returns control without leftover `offscreen_cef_subprocess.exe` processes.
- Runtime output directory contains the CEF resources needed for startup.

## Error Handling

- CMake fails early if `CEF_ROOT` is missing or incompatible.
- CEF initialization failure returns process exit code `1`.
- Runtime validation treats CEF fatal log entries and lingering subprocesses as stage 1 failures.
