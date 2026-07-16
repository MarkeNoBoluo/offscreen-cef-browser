# V2.1 Browser Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create a Windows x64 Qt + CEF windowless browser instance, load the configured URL, track lifecycle/load/title state, and forward resize events to CEF without requiring visible page rendering yet.

**Architecture:** Keep the V1 two-executable layout. Add a focused browser layer (`BrowserService`, `BrowserClient`, `MinimalRenderHandler`) and a Qt surface (`BrowserWidget`) that owns the native parent window and reports size changes; V2.2 will replace the minimal render handler with real frame copying and painting.

**Tech Stack:** C++17, CMake 3.21+, Visual Studio 2017 x64 generator, Qt 5.14.2 MSVC2017 64bit, CEF 96.0.18 Windows x64 VS2017 binary distribution, CTest.

## Global Constraints

- Windows x64 only.
- Qt 5.14.2 MSVC2017 64bit.
- CEF 96 windows64 VS2017 binary distribution unless a later explicit decision changes the V1/V2 stack.
- CMake-generated Visual Studio 2017 x64 build.
- Software OSR path remains selected; this plan only adds the minimum render handler needed for windowless browser creation.
- Do not add Qt 6.9.3 support.
- Do not add win32/x86 support.
- Do not add linux-amd64 support.
- Do not add `QOpenGLWidget`, shared texture, or accelerated paint support.
- Preserve the existing local default URL `https://baidu.com/` in `src/app/app_config.h` and update tests to match it.
- Do not commit changes unless the user explicitly asks for a commit.

---

## File Structure

- `src/browser/browser_geometry.h`: small Qt-free helper types and clamping function for CEF view rectangles.
- `src/browser/browser_geometry.cpp`: implementation of `ViewRectFromWidgetSize`.
- `tests/browser_geometry_tests.cpp`: unit tests for minimum valid view sizes used by CEF OSR.
- `tests/app_config_tests.cpp`: update default URL assertions to match the current `AppConfig` default.
- `src/qt/browser_widget.h`: `QWidget` native surface for the off-screen browser and resize notification boundary.
- `src/qt/browser_widget.cpp`: widget attributes, `HWND` extraction, current DIP view rect, and resize callback.
- `src/browser/minimal_render_handler.h`: CEF OSR render handler with view-rect/screen-info support and no frame storage.
- `src/browser/minimal_render_handler.cpp`: minimal `GetViewRect`, `GetScreenInfo`, and no-op `OnPaint` implementation.
- `src/browser/browser_client.h`: CEF client aggregating lifespan, load, and display callbacks.
- `src/browser/browser_client.cpp`: browser lifecycle, load error, title, and render-process termination forwarding.
- `src/browser/browser_service.h`: high-level browser lifecycle and navigation interface consumed by Qt.
- `src/browser/browser_service.cpp`: `CefBrowserHost::CreateBrowser`, resize, navigation, stop/reload, and shutdown behavior.
- `src/app/main.cpp`: replace the empty `QMainWindow` central area with `BrowserWidget`, create the browser after Qt shows the window, and delay Qt close until CEF reports browser closure.
- `src/CMakeLists.txt`: add browser and Qt sources to `offscreen_cef_browser`.
- `CMakeLists.txt`: add geometry source and test to `offscreen_core` / `offscreen_core_tests`.

## Task 1: Geometry Helper And Current Default URL Test Alignment

**Files:**
- Create: `src/browser/browser_geometry.h`
- Create: `src/browser/browser_geometry.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/app_config_tests.cpp`
- Create: `tests/browser_geometry_tests.cpp`

**Interfaces:**
- Consumes: `AppConfig::initial_url` default currently set to `https://baidu.com/`.
- Produces: `offscreen::BrowserViewRect` and `offscreen::ViewRectFromWidgetSize(int width, int height) -> BrowserViewRect`.

- [ ] **Step 1: Add the failing geometry test and update default URL expectations**

Create `tests/browser_geometry_tests.cpp` with this content:

```cpp
#include "browser/browser_geometry.h"

#include <cstdlib>
#include <iostream>

namespace {

void expect_eq(int actual, int expected, const char* label) {
  if (actual != expected) {
    std::cerr << label << " expected [" << expected << "] but got [" << actual
              << "]\n";
    std::exit(1);
  }
}

void test_uses_widget_size_when_positive() {
  const offscreen::BrowserViewRect rect =
      offscreen::ViewRectFromWidgetSize(1024, 768);

  expect_eq(rect.x, 0, "x");
  expect_eq(rect.y, 0, "y");
  expect_eq(rect.width, 1024, "width");
  expect_eq(rect.height, 768, "height");
}

void test_clamps_zero_size_to_one_pixel() {
  const offscreen::BrowserViewRect rect = offscreen::ViewRectFromWidgetSize(0, 0);

  expect_eq(rect.x, 0, "x");
  expect_eq(rect.y, 0, "y");
  expect_eq(rect.width, 1, "width");
  expect_eq(rect.height, 1, "height");
}

void test_clamps_negative_size_to_one_pixel() {
  const offscreen::BrowserViewRect rect =
      offscreen::ViewRectFromWidgetSize(-10, -20);

  expect_eq(rect.x, 0, "x");
  expect_eq(rect.y, 0, "y");
  expect_eq(rect.width, 1, "width");
  expect_eq(rect.height, 1, "height");
}

}  // namespace

int main() {
  test_uses_widget_size_when_positive();
  test_clamps_zero_size_to_one_pixel();
  test_clamps_negative_size_to_one_pixel();
  return 0;
}
```

Update `tests/app_config_tests.cpp` default expectations to preserve the current local default URL:

```cpp
void test_uses_default_url_when_argument_is_missing() {
  const char* argv[] = {"offscreen_cef_browser"};

  const offscreen::AppConfig config = offscreen::AppConfig::FromArgs(1, argv);

  expect_eq(config.initial_url, "https://baidu.com/", "initial_url");
}
```

```cpp
void test_ignores_empty_url_argument() {
  const char* argv[] = {"offscreen_cef_browser", "--url="};

  const offscreen::AppConfig config = offscreen::AppConfig::FromArgs(2, argv);

  expect_eq(config.initial_url, "https://baidu.com/", "initial_url");
}
```

- [ ] **Step 2: Wire the failing test into CMake**

Modify the top-level `CMakeLists.txt` core target and test section to include geometry. Keep `offscreen_core_tests` as a buildable aggregate target because the existing validation command builds that target by name:

```cmake
add_library(offscreen_core STATIC
  src/app/app_config.cpp
  src/app/app_config.h
  src/browser/browser_geometry.cpp
  src/browser/browser_geometry.h
)
```

```cmake
add_executable(app_config_tests
  tests/app_config_tests.cpp
)
target_link_libraries(app_config_tests PRIVATE offscreen_core)
target_compile_features(app_config_tests PRIVATE cxx_std_17)

add_executable(browser_geometry_tests
  tests/browser_geometry_tests.cpp
)
target_link_libraries(browser_geometry_tests PRIVATE offscreen_core)
target_compile_features(browser_geometry_tests PRIVATE cxx_std_17)

add_custom_target(offscreen_core_tests
  DEPENDS app_config_tests browser_geometry_tests
)

add_test(NAME app_config_tests COMMAND app_config_tests)
add_test(NAME browser_geometry_tests COMMAND browser_geometry_tests)
```

- [ ] **Step 3: Run tests to verify the new helper is missing**

Run:

```powershell
cmake --build "build\v1-cef96-msvc2017-x64" --config Debug --target offscreen_core_tests
```

Expected: build fails because `src/browser/browser_geometry.h` does not exist yet.

- [ ] **Step 4: Add the geometry helper implementation**

Create `src/browser/browser_geometry.h`:

```cpp
#pragma once

namespace offscreen {

struct BrowserViewRect {
  int x = 0;
  int y = 0;
  int width = 1;
  int height = 1;
};

BrowserViewRect ViewRectFromWidgetSize(int width, int height);

}  // namespace offscreen
```

Create `src/browser/browser_geometry.cpp`:

```cpp
#include "browser/browser_geometry.h"

#include <algorithm>

namespace offscreen {

BrowserViewRect ViewRectFromWidgetSize(int width, int height) {
  BrowserViewRect rect;
  rect.width = std::max(width, 1);
  rect.height = std::max(height, 1);
  return rect;
}

}  // namespace offscreen
```

- [ ] **Step 5: Run unit tests**

Run:

```powershell
cmake --build "build\v1-cef96-msvc2017-x64" --config Debug --target offscreen_core_tests; if ($?) { ctest --test-dir "build\v1-cef96-msvc2017-x64" -C Debug --output-on-failure }
```

Expected: `app_config_tests` and `browser_geometry_tests` build, and CTest reports `100% tests passed`.

- [ ] **Step 6: Review diff without committing**

Run:

```powershell
git diff -- CMakeLists.txt src/browser tests
```

Expected: diff contains only the geometry helper, its tests, and default URL assertion alignment. Do not run `git commit` unless the user explicitly asks.

## Task 2: Qt Browser Surface

**Files:**
- Create: `src/qt/browser_widget.h`
- Create: `src/qt/browser_widget.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `offscreen::ViewRectFromWidgetSize(int width, int height)`.
- Produces: `offscreen::BrowserWidget`, `BrowserWidget::NativeParentHandle() -> HWND`, `BrowserWidget::CurrentViewRect() -> BrowserViewRect`, and `BrowserWidget::SetResizeCallback(std::function<void(BrowserViewRect)>)`.

- [ ] **Step 1: Add the widget source files to the app target before creating them**

Modify `src/CMakeLists.txt` `OFFSCREEN_APP_SRCS`:

```cmake
set(OFFSCREEN_APP_SRCS
  app/app_config.cpp
  app/app_config.h
  app/browser_app.cpp
  app/browser_app.h
  app/main.cpp
  browser/browser_geometry.cpp
  browser/browser_geometry.h
  qt/browser_widget.cpp
  qt/browser_widget.h
)
```

- [ ] **Step 2: Run build to verify the missing widget fails**

Run:

```powershell
cmake --build "build\v1-cef96-msvc2017-x64" --config Debug --target offscreen_cef_browser
```

Expected: build fails because `src/qt/browser_widget.cpp` and `src/qt/browser_widget.h` do not exist yet.

- [ ] **Step 3: Create the BrowserWidget header**

Create `src/qt/browser_widget.h`:

```cpp
#pragma once

#include <functional>

#include <QWidget>
#include <windows.h>

#include "browser/browser_geometry.h"

namespace offscreen {

class BrowserWidget final : public QWidget {
 public:
  using ResizeCallback = std::function<void(BrowserViewRect)>;

  explicit BrowserWidget(QWidget* parent = nullptr);

  HWND NativeParentHandle() const;
  BrowserViewRect CurrentViewRect() const;
  void SetResizeCallback(ResizeCallback resize_callback);

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  ResizeCallback resize_callback_;
};

}  // namespace offscreen
```

- [ ] **Step 4: Create the BrowserWidget implementation**

Create `src/qt/browser_widget.cpp`:

```cpp
#include "qt/browser_widget.h"

#include <utility>

#include <QResizeEvent>

namespace offscreen {

BrowserWidget::BrowserWidget(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_NativeWindow, true);
  setAttribute(Qt::WA_DontCreateNativeAncestors, false);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
}

HWND BrowserWidget::NativeParentHandle() const {
  return reinterpret_cast<HWND>(winId());
}

BrowserViewRect BrowserWidget::CurrentViewRect() const {
  return ViewRectFromWidgetSize(width(), height());
}

void BrowserWidget::SetResizeCallback(ResizeCallback resize_callback) {
  resize_callback_ = std::move(resize_callback);
}

void BrowserWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (resize_callback_) {
    resize_callback_(CurrentViewRect());
  }
}

}  // namespace offscreen
```

- [ ] **Step 5: Build the app target**

Run:

```powershell
cmake --build "build\v1-cef96-msvc2017-x64" --config Debug --target offscreen_cef_browser
```

Expected: `offscreen_cef_browser` builds successfully.

- [ ] **Step 6: Review diff without committing**

Run:

```powershell
git diff -- src/CMakeLists.txt src/qt
```

Expected: diff contains only `BrowserWidget` and the app source list update. Do not run `git commit` unless the user explicitly asks.

## Task 3: Minimal OSR Render Handler And CEF Client

**Files:**
- Create: `src/browser/minimal_render_handler.h`
- Create: `src/browser/minimal_render_handler.cpp`
- Create: `src/browser/browser_client.h`
- Create: `src/browser/browser_client.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `BrowserViewRect` from Task 1.
- Produces: `offscreen::MinimalRenderHandler`, `offscreen::BrowserClient`, and `BrowserClient::Delegate` callback interface used by `BrowserService`.

- [ ] **Step 1: Add the browser source files to CMake before creating them**

Modify `src/CMakeLists.txt` `OFFSCREEN_APP_SRCS`:

```cmake
set(OFFSCREEN_APP_SRCS
  app/app_config.cpp
  app/app_config.h
  app/browser_app.cpp
  app/browser_app.h
  app/main.cpp
  browser/browser_client.cpp
  browser/browser_client.h
  browser/browser_geometry.cpp
  browser/browser_geometry.h
  browser/minimal_render_handler.cpp
  browser/minimal_render_handler.h
  qt/browser_widget.cpp
  qt/browser_widget.h
)
```

- [ ] **Step 2: Run build to verify missing browser files fail**

Run:

```powershell
cmake --build "build\v1-cef96-msvc2017-x64" --config Debug --target offscreen_cef_browser
```

Expected: build fails because the new `src/browser/*` files do not exist yet.

- [ ] **Step 3: Create the minimal render handler header**

Create `src/browser/minimal_render_handler.h`:

```cpp
#pragma once

#include "browser/browser_geometry.h"
#include "include/cef_render_handler.h"

namespace offscreen {

class MinimalRenderHandler final : public CefRenderHandler {
 public:
  explicit MinimalRenderHandler(BrowserViewRect view_rect);

  void SetViewRect(BrowserViewRect view_rect);
  BrowserViewRect view_rect() const;

  bool GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
  bool GetScreenInfo(CefRefPtr<CefBrowser> browser,
                     CefScreenInfo& screen_info) override;
  void OnPaint(CefRefPtr<CefBrowser> browser,
               PaintElementType type,
               const RectList& dirtyRects,
               const void* buffer,
               int width,
               int height) override;

 private:
  BrowserViewRect view_rect_;

  IMPLEMENT_REFCOUNTING(MinimalRenderHandler);
};

}  // namespace offscreen
```

- [ ] **Step 4: Create the minimal render handler implementation**

Create `src/browser/minimal_render_handler.cpp`:

```cpp
#include "browser/minimal_render_handler.h"

namespace offscreen {

MinimalRenderHandler::MinimalRenderHandler(BrowserViewRect view_rect)
    : view_rect_(view_rect) {}

void MinimalRenderHandler::SetViewRect(BrowserViewRect view_rect) {
  view_rect_ = view_rect;
}

BrowserViewRect MinimalRenderHandler::view_rect() const {
  return view_rect_;
}

bool MinimalRenderHandler::GetViewRect(CefRefPtr<CefBrowser> browser,
                                       CefRect& rect) {
  rect = CefRect(view_rect_.x, view_rect_.y, view_rect_.width, view_rect_.height);
  return true;
}

bool MinimalRenderHandler::GetScreenInfo(CefRefPtr<CefBrowser> browser,
                                         CefScreenInfo& screen_info) {
  screen_info.device_scale_factor = 1.0f;
  screen_info.rect = CefRect(view_rect_.x, view_rect_.y, view_rect_.width,
                             view_rect_.height);
  screen_info.available_rect = screen_info.rect;
  return true;
}

void MinimalRenderHandler::OnPaint(CefRefPtr<CefBrowser> browser,
                                   PaintElementType type,
                                   const RectList& dirtyRects,
                                   const void* buffer,
                                   int width,
                                   int height) {}

}  // namespace offscreen
```

- [ ] **Step 5: Create the browser client header**

Create `src/browser/browser_client.h`:

```cpp
#pragma once

#include <functional>
#include <string>

#include "browser/minimal_render_handler.h"
#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"

namespace offscreen {

class BrowserClient final : public CefClient,
                            public CefLifeSpanHandler,
                            public CefLoadHandler,
                            public CefDisplayHandler {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void OnBrowserCreated(CefRefPtr<CefBrowser> browser) = 0;
    virtual void OnBrowserClosing(CefRefPtr<CefBrowser> browser) = 0;
    virtual void OnBrowserClosed(CefRefPtr<CefBrowser> browser) = 0;
    virtual void OnLoadStateChanged(bool is_loading,
                                    bool can_go_back,
                                    bool can_go_forward) = 0;
    virtual void OnTitleChanged(const std::string& title) = 0;
    virtual void OnLoadErrorText(const std::string& error_text) = 0;
    virtual void OnRenderProcessTerminated() = 0;
  };

  BrowserClient(Delegate* delegate,
                CefRefPtr<MinimalRenderHandler> render_handler);

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
  CefRefPtr<CefLoadHandler> GetLoadHandler() override;
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;
  CefRefPtr<CefRenderHandler> GetRenderHandler() override;

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                            bool isLoading,
                            bool canGoBack,
                            bool canGoForward) override;
  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode errorCode,
                   const CefString& errorText,
                   const CefString& failedUrl) override;
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status) override;

 private:
  Delegate* delegate_ = nullptr;
  CefRefPtr<MinimalRenderHandler> render_handler_;

  IMPLEMENT_REFCOUNTING(BrowserClient);
};

}  // namespace offscreen
```

- [ ] **Step 6: Create the browser client implementation**

Create `src/browser/browser_client.cpp`:

```cpp
#include "browser/browser_client.h"

#include "include/cef_frame.h"

namespace offscreen {

namespace {

std::string CefStringToUtf8(const CefString& value) {
  return value.ToString();
}

}  // namespace

BrowserClient::BrowserClient(Delegate* delegate,
                             CefRefPtr<MinimalRenderHandler> render_handler)
    : delegate_(delegate), render_handler_(render_handler) {}

CefRefPtr<CefLifeSpanHandler> BrowserClient::GetLifeSpanHandler() {
  return this;
}

CefRefPtr<CefLoadHandler> BrowserClient::GetLoadHandler() {
  return this;
}

CefRefPtr<CefDisplayHandler> BrowserClient::GetDisplayHandler() {
  return this;
}

CefRefPtr<CefRenderHandler> BrowserClient::GetRenderHandler() {
  return render_handler_;
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  if (delegate_) {
    delegate_->OnBrowserCreated(browser);
  }
}

bool BrowserClient::DoClose(CefRefPtr<CefBrowser> browser) {
  if (delegate_) {
    delegate_->OnBrowserClosing(browser);
  }
  return false;
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  if (delegate_) {
    delegate_->OnBrowserClosed(browser);
  }
}

void BrowserClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                         bool isLoading,
                                         bool canGoBack,
                                         bool canGoForward) {
  if (delegate_) {
    delegate_->OnLoadStateChanged(isLoading, canGoBack, canGoForward);
  }
}

void BrowserClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                ErrorCode errorCode,
                                const CefString& errorText,
                                const CefString& failedUrl) {
  if (!frame || !frame->IsMain()) {
    return;
  }
  if (delegate_) {
    delegate_->OnLoadErrorText(CefStringToUtf8(errorText));
  }
}

void BrowserClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                  const CefString& title) {
  if (delegate_) {
    delegate_->OnTitleChanged(CefStringToUtf8(title));
  }
}

void BrowserClient::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                              TerminationStatus status) {
  if (delegate_) {
    delegate_->OnRenderProcessTerminated();
  }
}

}  // namespace offscreen
```

- [ ] **Step 7: Build the app target**

Run:

```powershell
cmake --build "build\v1-cef96-msvc2017-x64" --config Debug --target offscreen_cef_browser
```

Expected: `offscreen_cef_browser` builds successfully.

- [ ] **Step 8: Review diff without committing**

Run:

```powershell
git diff -- src/CMakeLists.txt src/browser
```

Expected: diff contains the minimal render handler, browser client, and source list update. Do not run `git commit` unless the user explicitly asks.

## Task 4: Browser Service And Main Window Integration

**Files:**
- Create: `src/browser/browser_service.h`
- Create: `src/browser/browser_service.cpp`
- Modify: `src/app/main.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `BrowserWidget::NativeParentHandle()`, `BrowserWidget::CurrentViewRect()`, `BrowserWidget::SetResizeCallback(...)`, `BrowserClient`, and `MinimalRenderHandler`.
- Produces: `offscreen::BrowserService::CreateBrowser(HWND, BrowserViewRect, const std::string&)`, `SetBrowserClosedCallback(BrowserClosedCallback)`, `Resize(BrowserViewRect)`, `Navigate(const std::string&)`, `Reload()`, `Stop()`, and `TryCloseBrowser()`.

- [ ] **Step 1: Add BrowserService sources to CMake before creating them**

Modify `src/CMakeLists.txt` `OFFSCREEN_APP_SRCS`:

```cmake
set(OFFSCREEN_APP_SRCS
  app/app_config.cpp
  app/app_config.h
  app/browser_app.cpp
  app/browser_app.h
  app/main.cpp
  browser/browser_client.cpp
  browser/browser_client.h
  browser/browser_geometry.cpp
  browser/browser_geometry.h
  browser/browser_service.cpp
  browser/browser_service.h
  browser/minimal_render_handler.cpp
  browser/minimal_render_handler.h
  qt/browser_widget.cpp
  qt/browser_widget.h
)
```

- [ ] **Step 2: Run build to verify BrowserService is missing**

Run:

```powershell
cmake --build "build\v1-cef96-msvc2017-x64" --config Debug --target offscreen_cef_browser
```

Expected: build fails because `browser/browser_service.cpp` and `browser/browser_service.h` do not exist yet.

- [ ] **Step 3: Create the BrowserService header**

Create `src/browser/browser_service.h`:

```cpp
#pragma once

#include <string>

#include <windows.h>

#include "browser/browser_client.h"
#include "browser/browser_geometry.h"
#include "browser/minimal_render_handler.h"
#include "include/cef_browser.h"

namespace offscreen {

class BrowserService final : public BrowserClient::Delegate {
 public:
  using BrowserClosedCallback = std::function<void()>;

  BrowserService();
  ~BrowserService() override;

  bool CreateBrowser(HWND parent_handle,
                     BrowserViewRect initial_view_rect,
                     const std::string& initial_url);
  void SetBrowserClosedCallback(BrowserClosedCallback browser_closed_callback);
  void Resize(BrowserViewRect view_rect);
  void Navigate(const std::string& url);
  void Reload();
  void Stop();
  bool TryCloseBrowser();

  bool has_browser() const;
  bool is_closing() const;
  std::string title() const;
  std::string last_error() const;

  void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBrowserClosing(CefRefPtr<CefBrowser> browser) override;
  void OnBrowserClosed(CefRefPtr<CefBrowser> browser) override;
  void OnLoadStateChanged(bool is_loading,
                          bool can_go_back,
                          bool can_go_forward) override;
  void OnTitleChanged(const std::string& title) override;
  void OnLoadErrorText(const std::string& error_text) override;
  void OnRenderProcessTerminated() override;

 private:
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<MinimalRenderHandler> render_handler_;
  CefRefPtr<BrowserClient> client_;
  bool is_closing_ = false;
  bool is_loading_ = false;
  bool can_go_back_ = false;
  bool can_go_forward_ = false;
  BrowserClosedCallback browser_closed_callback_;
  std::string title_;
  std::string last_error_;
};

}  // namespace offscreen
```

- [ ] **Step 4: Create the BrowserService implementation**

Create `src/browser/browser_service.cpp`:

```cpp
#include "browser/browser_service.h"

#include <utility>

#include "include/cef_frame.h"

namespace offscreen {

BrowserService::BrowserService() = default;

BrowserService::~BrowserService() = default;

void BrowserService::SetBrowserClosedCallback(
    BrowserClosedCallback browser_closed_callback) {
  browser_closed_callback_ = std::move(browser_closed_callback);
}

bool BrowserService::CreateBrowser(HWND parent_handle,
                                   BrowserViewRect initial_view_rect,
                                   const std::string& initial_url) {
  if (browser_ || client_) {
    return false;
  }

  render_handler_ = new MinimalRenderHandler(initial_view_rect);
  client_ = new BrowserClient(this, render_handler_);

  CefWindowInfo window_info;
  window_info.SetAsWindowless(parent_handle);

  CefBrowserSettings browser_settings;
  const bool created = CefBrowserHost::CreateBrowser(
      window_info, client_, initial_url, browser_settings, nullptr, nullptr);
  if (!created) {
    client_ = nullptr;
    render_handler_ = nullptr;
    last_error_ = "CefBrowserHost::CreateBrowser returned false";
  }
  return created;
}

void BrowserService::Resize(BrowserViewRect view_rect) {
  if (render_handler_) {
    render_handler_->SetViewRect(view_rect);
  }
  if (browser_) {
    browser_->GetHost()->WasResized();
  }
}

void BrowserService::Navigate(const std::string& url) {
  if (browser_ && browser_->GetMainFrame()) {
    browser_->GetMainFrame()->LoadURL(url);
  }
}

void BrowserService::Reload() {
  if (browser_) {
    browser_->Reload();
  }
}

void BrowserService::Stop() {
  if (browser_) {
    browser_->StopLoad();
  }
}

bool BrowserService::TryCloseBrowser() {
  if (!browser_) {
    return true;
  }
  is_closing_ = true;
  browser_->GetHost()->TryCloseBrowser();
  return false;
}

bool BrowserService::has_browser() const {
  return browser_ != nullptr;
}

bool BrowserService::is_closing() const {
  return is_closing_;
}

std::string BrowserService::title() const {
  return title_;
}

std::string BrowserService::last_error() const {
  return last_error_;
}

void BrowserService::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  last_error_.clear();
}

void BrowserService::OnBrowserClosing(CefRefPtr<CefBrowser> browser) {
  is_closing_ = true;
}

void BrowserService::OnBrowserClosed(CefRefPtr<CefBrowser> browser) {
  if (browser_ && browser_->IsSame(browser)) {
    browser_ = nullptr;
  }
  client_ = nullptr;
  render_handler_ = nullptr;
  if (browser_closed_callback_) {
    browser_closed_callback_();
  }
}

void BrowserService::OnLoadStateChanged(bool is_loading,
                                        bool can_go_back,
                                        bool can_go_forward) {
  is_loading_ = is_loading;
  can_go_back_ = can_go_back;
  can_go_forward_ = can_go_forward;
}

void BrowserService::OnTitleChanged(const std::string& title) {
  title_ = title;
}

void BrowserService::OnLoadErrorText(const std::string& error_text) {
  last_error_ = error_text;
}

void BrowserService::OnRenderProcessTerminated() {
  last_error_ = "CEF render process terminated";
}

}  // namespace offscreen
```

- [ ] **Step 5: Integrate BrowserWidget and BrowserService in main**

Modify `src/app/main.cpp` includes by adding:

```cpp
#include <memory>
```

```cpp
#include <QCloseEvent>
```

```cpp
#include "browser/browser_service.h"
#include "qt/browser_widget.h"
```

Add this helper class inside the anonymous namespace after `AssignCefString`:

```cpp
class BrowserMainWindow final : public QMainWindow {
 public:
  explicit BrowserMainWindow(offscreen::BrowserService* browser_service)
      : browser_service_(browser_service) {}

 protected:
  void closeEvent(QCloseEvent* event) override {
    if (browser_service_ && !browser_service_->TryCloseBrowser()) {
      event->ignore();
      return;
    }
    QMainWindow::closeEvent(event);
  }

 private:
  offscreen::BrowserService* browser_service_ = nullptr;
};
```

Replace the empty window block:

```cpp
QMainWindow main_window;
main_window.setWindowTitle(
    QStringLiteral("Offscreen CEF Browser - %1")
        .arg(StringToQString(app_config.initial_url)));
main_window.resize(1024, 768);
main_window.show();
```

with:

```cpp
offscreen::BrowserService browser_service;
BrowserMainWindow main_window(&browser_service);
main_window.setWindowTitle(
    QStringLiteral("Offscreen CEF Browser - %1")
        .arg(StringToQString(app_config.initial_url)));

auto browser_widget = std::make_unique<offscreen::BrowserWidget>();
offscreen::BrowserWidget* browser_widget_ptr = browser_widget.get();
main_window.setCentralWidget(browser_widget.release());

browser_widget_ptr->SetResizeCallback(
    [&browser_service](offscreen::BrowserViewRect view_rect) {
      browser_service.Resize(view_rect);
    });
browser_service.SetBrowserClosedCallback([&main_window]() {
  QTimer::singleShot(0, &main_window, [&main_window]() { main_window.close(); });
});

main_window.resize(1024, 768);
main_window.show();

QTimer::singleShot(0, &main_window, [&browser_service, browser_widget_ptr,
                                     initial_url = app_config.initial_url]() {
  browser_service.CreateBrowser(browser_widget_ptr->NativeParentHandle(),
                                browser_widget_ptr->CurrentViewRect(),
                                initial_url);
});
```

- [ ] **Step 6: Build browser and subprocess targets**

Run:

```powershell
cmake --build "build\v1-cef96-msvc2017-x64" --config Debug --target offscreen_cef_browser offscreen_cef_subprocess
```

Expected: both targets build successfully.

- [ ] **Step 7: Run CTest**

Run:

```powershell
ctest --test-dir "build\v1-cef96-msvc2017-x64" -C Debug --output-on-failure
```

Expected: `100% tests passed`.

- [ ] **Step 8: Review diff without committing**

Run:

```powershell
git diff -- src/app/main.cpp src/CMakeLists.txt src/browser src/qt tests CMakeLists.txt
```

Expected: diff contains only V2.1 browser-core changes. Do not run `git commit` unless the user explicitly asks.

## Task 5: Runtime Browser Creation Validation

**Files:**
- Modify only if runtime validation exposes a V2.1 defect: `src/app/main.cpp`, `src/browser/browser_service.cpp`, `src/browser/browser_client.cpp`, `src/browser/minimal_render_handler.cpp`, `src/qt/browser_widget.cpp`

**Interfaces:**
- Consumes: Debug executables from Task 4.
- Produces: evidence that the app creates a windowless CEF browser, starts navigation to the default URL, handles resize, and exits without lingering subprocesses.

- [ ] **Step 1: Confirm executable paths exist**

Run:

```powershell
Test-Path -LiteralPath "build\v1-cef96-msvc2017-x64\src\Debug\offscreen_cef_browser.exe"; Test-Path -LiteralPath "build\v1-cef96-msvc2017-x64\src\Debug\offscreen_cef_subprocess.exe"
```

Expected: two `True` lines.

- [ ] **Step 2: Launch with a known lightweight page**

Run:

```powershell
& "build\v1-cef96-msvc2017-x64\src\Debug\offscreen_cef_browser.exe" --url=https://example.com/
```

Expected: a Qt window titled `Offscreen CEF Browser - https://example.com/` appears and remains open. Page contents are not expected to be visible in V2.1 because `OnPaint` is intentionally a no-op.

- [ ] **Step 3: Manually validate resize stability**

Resize the Qt window larger and smaller three times.

Expected: the process remains alive and responsive; no crash dialog appears.

- [ ] **Step 4: Close the window and confirm subprocess cleanup**

After closing the Qt window, run:

```powershell
Get-Process -Name "offscreen_cef_subprocess" -ErrorAction SilentlyContinue
```

Expected: no process output.

- [ ] **Step 5: Inspect CEF log for browser creation failures**

Run:

```powershell
rg -in "CreateBrowser|FATAL|Check failed|error|termination" "build\v1-cef96-msvc2017-x64\src\Debug\cef.log"
```

Expected: no fatal initialization failure, no failed browser creation message, and no render-process termination caused by V2.1 code. Non-fatal network errors from the loaded URL can be recorded in the final summary.

- [ ] **Step 6: Re-run automated validation after any runtime fix**

Run:

```powershell
cmake --build "build\v1-cef96-msvc2017-x64" --config Debug --target offscreen_core_tests offscreen_cef_browser offscreen_cef_subprocess; if ($?) { ctest --test-dir "build\v1-cef96-msvc2017-x64" -C Debug --output-on-failure }
```

Expected: build succeeds and CTest reports `100% tests passed`.

- [ ] **Step 7: Final working-tree review without committing**

Run:

```powershell
git status --short; git diff -- CMakeLists.txt src tests docs/superpowers
```

Expected: only intended V2.1 source/test/doc changes are present. Do not run `git commit` unless the user explicitly asks.

## Self-Review

- Spec coverage: This plan covers V2.1 Browser Core by adding `BrowserService`, `BrowserClient`, minimal lifespan/load/display handlers, a `BrowserWidget` central surface, windowless `CefBrowserHost::CreateBrowser`, initial URL loading, and resize propagation. It intentionally leaves visible painting, popup composition, input forwarding, IME, browser UX controls, compatibility pages, and deployment hardening to V2.2-V2.6.
- Placeholder scan: The plan contains concrete file paths, exact interfaces, exact command lines, expected results, and code blocks for each source change.
- Type consistency: `BrowserViewRect`, `ViewRectFromWidgetSize`, `BrowserWidget`, `MinimalRenderHandler`, `BrowserClient::Delegate`, and `BrowserService` names match across tasks and later integration steps.
- Repository constraint check: Commit steps are replaced with diff-review steps because repository instructions say not to commit unless the user explicitly asks.
