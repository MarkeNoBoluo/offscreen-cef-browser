# V1 Stage 1 Closure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the V1 Qt + CEF engineering skeleton configures, builds, starts an empty Qt main window, and exits cleanly with CEF 96 VS2017 x64.

**Architecture:** Keep the current two-executable architecture: `offscreen_cef_browser` as the Qt browser-process shell and `offscreen_cef_subprocess` as the CEF subprocess entry. Stage 1 validates process startup, CEF initialization, external message pump wiring, runtime resource discovery, and shutdown without creating a `CefBrowser`.

**Tech Stack:** C++17, CMake 3.21+, Visual Studio 2017 x64 generator, Qt 5.14.2 MSVC2017 64bit, CEF 96.0.18 Windows x64 VS2017 binary distribution, CTest.

## Global Constraints

- Windows x64 only.
- Qt 5.14.2 MSVC2017 64bit only.
- CEF binary distribution: `D:\Git\cef_binary_96.0.18+gfe551e4+chromium-96.0.4664.110_windows64_vs2017`.
- Build directory: `build\v1-cef96-msvc2017-x64`.
- Do not create a `CefBrowser` instance in this stage.
- Do not implement `BrowserService`, `BrowserClient`, `CefRenderHandler`, painting, input, DPI, IME, DevTools, downloads, or navigation.
- Do not commit changes unless the user explicitly asks for a commit.

---

## File Structure

- `CMakeLists.txt`: top-level project, core test target, CEF/Qt discovery, CEF version/toolchain guard, and app subdirectory inclusion.
- `src/CMakeLists.txt`: application and subprocess targets, CEF wrapper linkage, CEF runtime/resource copy rules, and install rules.
- `src/app/main.cpp`: browser-process entry, Qt application setup, `CefInitialize`, empty `QMainWindow`, Qt event loop, and `CefShutdown`.
- `src/app/browser_app.h`: `BrowserApp` interface for CEF app and browser-process handler callbacks.
- `src/app/browser_app.cpp`: CEF command-line switch setup and external message pump scheduling.
- `src/subprocess/subprocess_main.cpp`: subprocess executable entry and `CefExecuteProcess` call.
- `src/app/app_config.h`: minimal startup configuration interface.
- `src/app/app_config.cpp`: command-line parsing for the initial URL, used only for title text in stage 1.
- `tests/app_config_tests.cpp`: core unit tests for startup configuration parsing.

## Task 1: Environment And Path Verification

**Files:**
- Read: `CMakeLists.txt`
- Read: `src/CMakeLists.txt`
- Read: `src/app/main.cpp`
- No source modifications expected.

**Interfaces:**
- Consumes: existing CMake options `OFFSCREEN_BUILD_APP`, `OFFSCREEN_BUILD_TESTS`, and cache variable `CEF_ROOT`.
- Produces: verified local paths and command inputs for Task 2.

- [ ] **Step 1: Confirm CEF root exists**

Run:

```powershell
Test-Path -LiteralPath "D:\Git\cef_binary_96.0.18+gfe551e4+chromium-96.0.4664.110_windows64_vs2017"
```

Expected: `True`.

- [ ] **Step 2: Confirm CEF CMake and headers exist**

Run:

```powershell
Test-Path -LiteralPath "D:\Git\cef_binary_96.0.18+gfe551e4+chromium-96.0.4664.110_windows64_vs2017\cmake\FindCEF.cmake"; Test-Path -LiteralPath "D:\Git\cef_binary_96.0.18+gfe551e4+chromium-96.0.4664.110_windows64_vs2017\include\cef_version.h"
```

Expected: two `True` lines.

- [ ] **Step 3: Confirm Qt discovery source**

Run:

```powershell
where.exe qmake
```

Expected: at least one `qmake.exe` path. If the first path is not from Qt 5.14.2 MSVC2017 64bit, put the correct Qt 5.14.2 MSVC2017 64bit `bin` directory first in `PATH` before continuing.

- [ ] **Step 4: Confirm Qt version and prefix**

Run:

```powershell
qmake -query QT_VERSION; qmake -query QT_INSTALL_PREFIX
```

Expected: first line is `5.14.2`; second line is the Qt 5.14.2 MSVC2017 64bit prefix.

- [ ] **Step 5: Confirm VS2017 generator availability**

Run:

```powershell
cmake -G
```

Expected: output includes `Visual Studio 15 2017`.

- [ ] **Step 6: Review before proceeding**

Proceed to Task 2 only when the CEF root, Qt prefix, and VS2017 generator checks match the target stack.

## Task 2: Configure And Build Debug Targets

**Files:**
- Modify only if configure/build errors require it: `CMakeLists.txt`
- Modify only if configure/build errors require it: `src/CMakeLists.txt`
- Modify only if compile errors require it: `src/app/main.cpp`
- Modify only if compile errors require it: `src/app/browser_app.h`
- Modify only if compile errors require it: `src/app/browser_app.cpp`
- Modify only if compile errors require it: `src/subprocess/subprocess_main.cpp`
- Test: `tests/app_config_tests.cpp`

**Interfaces:**
- Consumes: verified `CEF_ROOT` and Qt prefix from Task 1.
- Produces: Debug executables `build\v1-cef96-msvc2017-x64\src\Debug\offscreen_cef_browser.exe` and `build\v1-cef96-msvc2017-x64\src\Debug\offscreen_cef_subprocess.exe`.

- [ ] **Step 1: Configure with VS2017 x64**

Run this command when `qmake -query QT_INSTALL_PREFIX` already points to Qt 5.14.2 MSVC2017 64bit:

```powershell
cmake -S . -B "build\v1-cef96-msvc2017-x64" -G "Visual Studio 15 2017" -A x64 -DCEF_ROOT="D:\Git\cef_binary_96.0.18+gfe551e4+chromium-96.0.4664.110_windows64_vs2017" -DOFFSCREEN_BUILD_APP=ON -DOFFSCREEN_BUILD_TESTS=ON
```

Expected: configure and generate complete successfully.

- [ ] **Step 2: Configure with explicit Qt prefix when needed**

Run this command only when Task 1 found the correct Qt prefix but CMake did not find Qt automatically:

```powershell
$qtPrefix = qmake -query QT_INSTALL_PREFIX; cmake -S . -B "build\v1-cef96-msvc2017-x64" -G "Visual Studio 15 2017" -A x64 -DCEF_ROOT="D:\Git\cef_binary_96.0.18+gfe551e4+chromium-96.0.4664.110_windows64_vs2017" -DCMAKE_PREFIX_PATH="$qtPrefix" -DOFFSCREEN_BUILD_APP=ON -DOFFSCREEN_BUILD_TESTS=ON
```

Expected: configure and generate complete successfully using the exact prefix printed by `qmake -query QT_INSTALL_PREFIX`.

- [ ] **Step 3: Fix configure failures with minimal edits**

If configure fails inside project-owned files, edit only the directly failing line. Known acceptable fixes are limited to these patterns:

```cmake
# Keep this compatibility guard for CEF 96 and VS2017.
if(MSVC AND CEF_VERSION_MAJOR_VALUE GREATER_EQUAL 142 AND MSVC_VERSION LESS 1930)
  message(FATAL_ERROR
    "CEF ${CEF_VERSION_MAJOR_VALUE} requires Visual Studio 2022 or a compatible newer MSVC toolchain. "
    "Use a CEF binary distribution compatible with Qt 5.14.2 MSVC2017 64bit, "
    "or move V1 to a newer MSVC toolchain.")
elseif(MSVC AND CEF_VERSION_MAJOR_VALUE GREATER_EQUAL 102 AND MSVC_VERSION LESS 1920)
  message(FATAL_ERROR
    "CEF ${CEF_VERSION_MAJOR_VALUE} requires Visual Studio 2019 or newer according to its binary distribution CMake files. "
    "V1 is fixed to Qt 5.14.2 MSVC2017 64bit, so use a CEF binary distribution built for VS2017/x64.")
endif()
```

Expected: no broad CMake restructuring; no changes for Qt 6, win32, or Linux.

- [ ] **Step 4: Build Debug configuration**

Run:

```powershell
cmake --build "build\v1-cef96-msvc2017-x64" --config Debug --target offscreen_core_tests offscreen_cef_browser offscreen_cef_subprocess
```

Expected: all three targets build successfully.

- [ ] **Step 5: Fix compile/link failures with minimal edits**

If compilation fails, update only the smallest affected function or target definition. Keep these stage 1 invariants in source code:

```cpp
CefEnableHighDPISupport();
CefMainArgs main_args(::GetModuleHandleW(nullptr));
const int exit_code = CefExecuteProcess(main_args, cef_app.get(), nullptr);
if (exit_code >= 0) {
  return exit_code;
}
```

```cpp
settings.no_sandbox = true;
settings.external_message_pump = true;
settings.windowless_rendering_enabled = true;
```

```cpp
QMainWindow main_window;
main_window.resize(1024, 768);
main_window.show();
```

Expected: fixes do not introduce a `CefBrowser`, `BrowserService`, render handler, or input handling.

- [ ] **Step 6: Run unit tests**

Run:

```powershell
ctest --test-dir "build\v1-cef96-msvc2017-x64" -C Debug --output-on-failure
```

Expected: `100% tests passed`.

- [ ] **Step 7: Review diff before runtime validation**

Run:

```powershell
git diff -- CMakeLists.txt src tests docs/superpowers
```

Expected: changes are limited to stage 1 build/startup validation and the approved spec/plan documents.

## Task 3: Runtime Startup And Shutdown Validation

**Files:**
- Modify only if runtime errors require it: `src/CMakeLists.txt`
- Modify only if runtime errors require it: `src/app/main.cpp`
- No test source modifications expected.

**Interfaces:**
- Consumes: Debug executables from Task 2.
- Produces: evidence that `offscreen_cef_browser.exe` starts, shows an empty Qt main window, writes `cef.log`, and exits without lingering subprocesses.

- [ ] **Step 1: Confirm runtime executable paths exist**

Run:

```powershell
Test-Path -LiteralPath "build\v1-cef96-msvc2017-x64\src\Debug\offscreen_cef_browser.exe"; Test-Path -LiteralPath "build\v1-cef96-msvc2017-x64\src\Debug\offscreen_cef_subprocess.exe"
```

Expected: two `True` lines.

- [ ] **Step 2: Confirm copied CEF runtime files exist**

Run:

```powershell
Test-Path -LiteralPath "build\v1-cef96-msvc2017-x64\src\Debug\libcef.dll"; Test-Path -LiteralPath "build\v1-cef96-msvc2017-x64\src\Debug\icudtl.dat"; Test-Path -LiteralPath "build\v1-cef96-msvc2017-x64\src\Debug\locales"
```

Expected: three `True` lines.

- [ ] **Step 3: Launch the app for manual window validation**

Run:

```powershell
& "build\v1-cef96-msvc2017-x64\src\Debug\offscreen_cef_browser.exe" --url=https://example.com/
```

Expected: a window titled `Offscreen CEF Browser - https://example.com/` appears. Close the window manually.

- [ ] **Step 4: Confirm subprocess cleanup**

Run after closing the window:

```powershell
Get-Process -Name "offscreen_cef_subprocess" -ErrorAction SilentlyContinue
```

Expected: no process output.

- [ ] **Step 5: Confirm CEF log exists**

Run:

```powershell
Test-Path -LiteralPath "build\v1-cef96-msvc2017-x64\src\Debug\cef.log"
```

Expected: `True`.

- [ ] **Step 6: Inspect CEF log for fatal initialization errors**

Run:

```powershell
rg -in "FATAL|Check failed|error" "build\v1-cef96-msvc2017-x64\src\Debug\cef.log"
```

Expected: no matches for fatal initialization failures. Non-fatal warnings unrelated to startup can be recorded in the final summary.

- [ ] **Step 7: Re-run automated validation after any runtime fix**

Run:

```powershell
cmake --build "build\v1-cef96-msvc2017-x64" --config Debug --target offscreen_core_tests offscreen_cef_browser offscreen_cef_subprocess; if ($?) { ctest --test-dir "build\v1-cef96-msvc2017-x64" -C Debug --output-on-failure }
```

Expected: build succeeds and CTest passes.

- [ ] **Step 8: Final working-tree review**

Run:

```powershell
git status --short; git diff -- CMakeLists.txt src tests docs/superpowers
```

Expected: only intended stage 1 source/doc changes are present. Do not commit unless the user explicitly asks.

## Self-Review

- Spec coverage: Task 1 covers target stack and local path validation; Task 2 covers configure, Debug build, CTest, and minimal compatibility fixes; Task 3 covers app launch, empty Qt window, CEF log, runtime resources, and subprocess cleanup.
- Placeholder scan: the plan has no unresolved markers or unspecified code changes. The Qt prefix fallback uses a concrete PowerShell variable populated by `qmake -query QT_INSTALL_PREFIX`.
- Type consistency: C++ and CMake names match the existing codebase: `BrowserApp`, `AppConfig::FromArgs`, `offscreen_cef_browser`, `offscreen_cef_subprocess`, `CEF_ROOT`, and `OFFSCREEN_BUILD_TESTS`.
