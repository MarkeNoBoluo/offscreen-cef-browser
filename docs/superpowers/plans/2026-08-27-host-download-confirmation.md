# 宿主控制下载确认 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 `CefWebView` 和 `CefTabbedBrowser` 增加由宿主应用控制的下载确认流程，使宿主能异步选择完整保存路径、确认或取消下载，同时保持现有自动下载行为默认不变。

**Architecture:** `BrowserClient` 仍接收 CEF 100 的 `OnBeforeDownload`，但把下载 ID、建议文件名、来源 URL 和 `CefBeforeDownloadCallback` 交给 `BrowserService`。自动模式沿用当前立即 `Continue()` 的路径；宿主确认模式将 callback 暂存在一次性请求注册表中，通过不暴露 CEF 类型的 Qt 信号通知宿主，宿主随后调用接受或取消接口。`CefTabbedBrowser` 保存容器级下载配置并应用到现有、新建和 popup 标签，同时转发所有标签的下载请求。

**Tech Stack:** C++17、Qt 6.9.3 Widgets、CEF 100、CMake、CTest、MSVC 2022 x64。

**Spec:** `docs/superpowers/plans/2026-08-27-host-download-confirmation.md#已批准行为`，来自 2026-08-27 会话中已确认的“宿主负责对话框”和“默认保持自动下载”要求。

## Global Constraints

- 默认模式必须是 `DownloadDecisionMode::kAutomatic`，未启用确认模式的现有宿主行为和 `offscreen_cef_browser.exe` 不得改变。
- 下载对话框由宿主应用创建；SDK 不依赖或直接创建 `QFileDialog`。
- 宿主确认模式必须显式启用，并且宿主应先连接 `downloadRequested` 再启用该模式。
- 公共头文件不得暴露 `CefBeforeDownloadCallback`、`CefDownloadItem`、`BrowserService` 或其他 CEF 内部类型。
- CEF 100 的 `CefBeforeDownloadCallback` 只有 `Continue()`，没有 `Cancel()`；取消下载的实现是释放暂存 callback 且不调用 `Continue()`。
- 不得在 `OnBeforeDownload` 调用栈中执行阻塞式 `QFileDialog::getSaveFileName()`；宿主使用 `QFileDialog::open()` 异步处理。
- 接受下载时必须传入包含文件名的绝对完整路径；空路径或相对路径按取消处理。
- 同一 WebView 必须支持多个并发待确认请求；每个请求只能接受或取消一次。
- WebView/标签关闭时释放全部待确认 callback，不能让 CEF 关闭流程因未决请求挂起。
- `CefTabbedBrowser` 必须转发后台标签和 popup 标签的请求，并保留现有 `currentDownloadStateChanged` 信号以兼容旧宿主。
- 所有下载决策接口只在 Qt GUI/CEF browser-process UI 线程调用；跨线程宿主调用必须通过 `Qt::QueuedConnection` 投递。
- 只实现下载前确认、保存路径和取消；本次不增加进度条、暂停、继续、限速或下载历史。
- 构建、CTest 与静态检查不等于 GUI 下载验收；文件对话框、真实落盘、取消和标签关闭必须人工验证。
- 保留当前工作树已有的未跟踪文件 `docs/superpowers/plans/2026-08-19-touchscreen-gesture-interaction.md`，不得删除、覆盖或顺带提交。

## 已批准行为

1. `kAutomatic`：按当前逻辑立即调用 `Continue(default_or_configured_path, false)`，不发出 `downloadRequested`。
2. `kAskHost`：暂存 callback，发出 `downloadRequested`，在宿主响应前不调用 `Continue()`。
3. 宿主接受：使用宿主选择的绝对完整路径调用 `Continue(path, false)`，随后发出已有的开始状态 `downloadStateChanged(0, ...)`。
4. 宿主取消：从待确认注册表移除 callback，不调用 `Continue()`；CEF 按默认规则取消。
5. 重复接受、重复取消或未知请求 ID：记录诊断日志并保持无副作用。
6. 从 `kAskHost` 切回 `kAutomatic`、WebView 关闭或 `BrowserService` 销毁：释放全部未决 callback。
7. 不增加自动超时；宿主负责及时响应，SDK 负责在生命周期结束时兜底释放。

---

### Task 1: 增加公共下载决策类型和一次性请求注册表

**Files:**
- Create: `src/offscreen_cef/download_types.h`
- Create: `src/browser/download_request_registry.h`
- Create: `src/browser/download_request_registry.cpp`
- Modify: `CMakeLists.txt:39-66`
- Create: `tests/download_request_registry_tests.cpp`
- Modify: `tests/CMakeLists.txt:1-49`

**Interfaces:**
- Consumes: CEF `CefDownloadItem::GetId()` 提供的 `uint32` 下载 ID；本任务自身不依赖 CEF 或 Qt。
- Produces: `offscreen::DownloadRequestId`、`offscreen::DownloadDecisionMode` 和 `offscreen::DownloadRequestRegistry`，供 BrowserService 和公开 Qt API 共同使用。

- [ ] **Step 1: 先创建失败测试，固定一次性和并发语义。**

  创建 `tests/download_request_registry_tests.cpp`，测试至少包含以下可执行逻辑：

  ```cpp
  #include "browser/download_request_registry.h"

  #include <cstdlib>
  #include <iostream>
  #include <string>

  namespace {

  void expect_true(bool value, const char* label) {
    if (!value) {
      std::cerr << label << " expected true\n";
      std::exit(1);
    }
  }

  void expect_false(bool value, const char* label) {
    if (value) {
      std::cerr << label << " expected false\n";
      std::exit(1);
    }
  }

  void test_accepts_each_request_once() {
    offscreen::DownloadRequestRegistry registry;
    int continue_count = 0;
    std::wstring accepted_path;
    expect_true(registry.Add(7, [&](const std::wstring& path) {
      ++continue_count;
      accepted_path = path;
    }), "add request");
    expect_true(registry.Accept(7, L"C:\\Downloads\\report.pdf"),
                "accept request");
    expect_false(registry.Accept(7, L"C:\\Downloads\\second.pdf"),
                 "second accept");
    expect_true(continue_count == 1, "continue once");
    expect_true(accepted_path == L"C:\\Downloads\\report.pdf",
                "selected path forwarded");
  }

  void test_rejects_without_continuing() {
    offscreen::DownloadRequestRegistry registry;
    int continue_count = 0;
    registry.Add(8, [&](const std::wstring&) { ++continue_count; });
    expect_true(registry.Reject(8), "reject request");
    expect_false(registry.Reject(8), "second reject");
    expect_true(continue_count == 0, "reject does not continue");
  }

  void test_tracks_concurrent_requests_independently() {
    offscreen::DownloadRequestRegistry registry;
    int first = 0;
    int second = 0;
    registry.Add(10, [&](const std::wstring&) { ++first; });
    registry.Add(11, [&](const std::wstring&) { ++second; });
    expect_true(registry.Accept(11, L"C:\\Downloads\\b.txt"),
                "accept second");
    expect_true(registry.Reject(10), "reject first");
    expect_true(first == 0 && second == 1, "independent decisions");
    expect_true(registry.pending_count() == 0, "no pending requests");
  }

  void test_reject_all_releases_pending_requests() {
    offscreen::DownloadRequestRegistry registry;
    registry.Add(20, [](const std::wstring&) {});
    registry.Add(21, [](const std::wstring&) {});
    registry.RejectAll();
    expect_true(registry.pending_count() == 0, "reject all clears requests");
  }

  }  // namespace

  int main() {
    test_accepts_each_request_once();
    test_rejects_without_continuing();
    test_tracks_concurrent_requests_independently();
    test_reject_all_releases_pending_requests();
    return 0;
  }
  ```

- [ ] **Step 2: 注册测试并确认首次构建失败。**

  在 `tests/CMakeLists.txt` 增加：

  ```cmake
  add_offscreen_core_test(download_request_registry_tests
    download_request_registry_tests.cpp)
  ```

  并把该目标加入 `offscreen_core_tests` 的 `DEPENDS`。运行：

  ```powershell
  cmake -S . -B build\cef100-msvc2022-x64 `
    -G "Visual Studio 17 2022" -A x64 `
    -DCEF_ROOT="D:\Git\cef_binary_100.0.14+g4e5ba66+chromium-100.0.4896.75_windows64_minimal" `
    -DQt6_DIR="D:\IDE\QT6.9.3\6.9.3\msvc2022_64\lib\cmake\Qt6" `
    -DCEF_RUNTIME_LIBRARY_FLAG=/MD `
    -DOFFSCREEN_BUILD_APP=ON `
    -DOFFSCREEN_BUILD_TESTS=ON `
    -DOFFSCREEN_BUILD_EMBEDDING_DEMOS=ON
  cmake --build build\cef100-msvc2022-x64 --config Release `
    --target download_request_registry_tests
  ```

  Expected: FAIL，原因是 `browser/download_request_registry.h` 尚不存在。

- [ ] **Step 3: 定义不含 Qt/CEF 类型的公共契约。**

  `src/offscreen_cef/download_types.h`：

  ```cpp
  #pragma once

  #include <cstdint>

  namespace offscreen {

  using DownloadRequestId = std::uint32_t;

  enum class DownloadDecisionMode {
    kAutomatic,
    kAskHost,
  };

  }  // namespace offscreen
  ```

  `src/browser/download_request_registry.h` 对外提供以下精确接口：

  ```cpp
  #pragma once

  #include <cstddef>
  #include <functional>
  #include <string>
  #include <unordered_map>

  #include "offscreen_cef/download_types.h"

  namespace offscreen {

  class DownloadRequestRegistry final {
   public:
    using ContinueCallback = std::function<void(const std::wstring&)>;

    bool Add(DownloadRequestId id, ContinueCallback callback);
    bool Accept(DownloadRequestId id, const std::wstring& full_path);
    bool Reject(DownloadRequestId id);
    void RejectAll();
    std::size_t pending_count() const;

   private:
    std::unordered_map<DownloadRequestId, ContinueCallback> pending_;
  };

  }  // namespace offscreen
  ```

- [ ] **Step 4: 实现最小注册表逻辑。**

  `download_request_registry.cpp` 使用以下实现；`Accept()` 必须先从 map 中移动并删除 callback，再调用它，避免 `Continue()` 发生重入时 map 仍处于未决状态：

  ```cpp
  #include "browser/download_request_registry.h"

  #include <utility>

  namespace offscreen {

  bool DownloadRequestRegistry::Add(DownloadRequestId id,
                                    ContinueCallback callback) {
    if (!callback || pending_.find(id) != pending_.end()) {
      return false;
    }
    pending_.emplace(id, std::move(callback));
    return true;
  }

  bool DownloadRequestRegistry::Accept(DownloadRequestId id,
                                       const std::wstring& full_path) {
    auto it = pending_.find(id);
    if (it == pending_.end()) {
      return false;
    }
    ContinueCallback callback = std::move(it->second);
    pending_.erase(it);
    callback(full_path);
    return true;
  }

  bool DownloadRequestRegistry::Reject(DownloadRequestId id) {
    return pending_.erase(id) == 1;
  }

  void DownloadRequestRegistry::RejectAll() {
    pending_.clear();
  }

  std::size_t DownloadRequestRegistry::pending_count() const {
    return pending_.size();
  }

  }  // namespace offscreen
  ```

  不在线程间加锁，因为调用边界固定在 Qt GUI/CEF UI 线程。

- [ ] **Step 5: 把新源文件加入 `offscreen_core` 并运行测试。**

  在根 `CMakeLists.txt` 的 `offscreen_core` 源列表加入：

  ```cmake
  src/browser/download_request_registry.cpp
  src/browser/download_request_registry.h
  src/offscreen_cef/download_types.h
  ```

  运行：

  ```powershell
  cmake --build build\cef100-msvc2022-x64 --config Release `
    --target download_request_registry_tests
  ctest --test-dir build\cef100-msvc2022-x64 -C Release `
    -R download_request_registry_tests --output-on-failure
  ```

  Expected: `download_request_registry_tests` PASS。

- [ ] **Step 6: 提交纯逻辑和测试。**

  ```powershell
  git add CMakeLists.txt tests\CMakeLists.txt `
    tests\download_request_registry_tests.cpp `
    src\browser\download_request_registry.h `
    src\browser\download_request_registry.cpp `
    src\offscreen_cef\download_types.h
  git commit -m "test: add download request registry"
  ```

### Task 2: 将 CEF 下载前回调改为可选的宿主决策

**Files:**
- Modify: `src/browser/browser_client.h:28-74,170-185`
- Modify: `src/browser/browser_client.cpp:155-195`
- Modify: `src/browser/browser_service.h:30-47,96-101,337-379`
- Modify: `src/browser/browser_service.cpp:66-76,217-227,637-666,745-770`

**Interfaces:**
- Consumes: Task 1 的 `DownloadRequestId`、`DownloadDecisionMode` 和 `DownloadRequestRegistry`。
- Produces: `BrowserService::SetDownloadDecisionMode()`、`SetDownloadRequestCallback()`、`AcceptDownload()`、`CancelDownload()`，供 `CefWebView` 封装。

- [ ] **Step 1: 扩展 `BrowserClient::Delegate` 的下载请求参数。**

  将现有 `OnDownloadStarted(callback, suggested_name)` 改为：

  ```cpp
  virtual void OnDownloadRequested(
      DownloadRequestId id,
      CefRefPtr<CefBeforeDownloadCallback> callback,
      const std::string& suggested_name,
      const std::string& source_url) = 0;
  ```

  `browser_client.h` 包含 `offscreen_cef/download_types.h`。在 `OnBeforeDownload()` 中：

  ```cpp
  if (!download_item || !download_item->IsValid() || !callback) {
    return;
  }
  delegate_->OnDownloadRequested(
      download_item->GetId(), callback, CefStringToUtf8(suggested_name),
      CefStringToUtf8(download_item->GetOriginalUrl()));
  ```

  不得保存 `download_item`；CEF 头文件明确禁止在该回调外持有它。

- [ ] **Step 2: 在 `BrowserService` 声明宿主决策接口。**

  增加：

  ```cpp
  using DownloadRequestCallback =
      std::function<void(DownloadRequestId id,
                         const std::string& suggested_name,
                         const std::string& source_url)>;

  void SetDownloadDecisionMode(DownloadDecisionMode mode);
  void SetDownloadRequestCallback(DownloadRequestCallback callback);
  bool AcceptDownload(DownloadRequestId id, const std::wstring& full_path);
  bool CancelDownload(DownloadRequestId id);
  ```

  把 delegate override 改为与 Step 1 相同的 `OnDownloadRequested(...)`，并增加成员：

  ```cpp
  DownloadDecisionMode download_decision_mode_ =
      DownloadDecisionMode::kAutomatic;
  DownloadRequestCallback download_request_callback_;
  DownloadRequestRegistry pending_downloads_;
  ```

- [ ] **Step 3: 保留自动模式的原始行为。**

  在 `OnDownloadRequested()` 中先按当前规则构造自动路径：下载目录非空时为 `download_dir_ + L"\\" + suggested_name`，否则为空字符串。`kAutomatic` 分支必须立即：

  ```cpp
  callback->Continue(download_path, false);
  OnDownloadStateChanged(
      0, suggested_name, CefString(download_path).ToString());
  ```

  该分支不得写入 `pending_downloads_`，也不得调用 `download_request_callback_`。

- [ ] **Step 4: 实现宿主确认模式和一次性决策。**

  `kAskHost` 分支先把 continuation 加入注册表：

  ```cpp
  const bool added = pending_downloads_.Add(
      id, [this, callback, suggested_name](const std::wstring& full_path) {
        callback->Continue(full_path, false);
        OnDownloadStateChanged(
            0, suggested_name, CefString(full_path).ToString());
      });
  ```

  只有 `added == true` 且 `download_request_callback_` 存在时才调用：

  ```cpp
  download_request_callback_(id, suggested_name, source_url);
  ```

  重复 ID 或没有 request callback 时立即 `pending_downloads_.Reject(id)` 并记录诊断日志，不启动下载。

  `AcceptDownload()` 在 `full_path.empty()` 时转调 `CancelDownload()`，否则调用 `pending_downloads_.Accept()`；`CancelDownload()` 调用 `pending_downloads_.Reject()`。取消路径只能释放 before-download callback，不得调用取消成员函数。

- [ ] **Step 5: 在模式切换和浏览器关闭时清理未决请求。**

  `SetDownloadDecisionMode()` 从 `kAskHost` 切换到其他模式时先调用 `RejectAll()`。`TryCloseBrowser()` 在进入关闭状态机前调用 `RejectAll()`，保证打开下载对话框时关闭标签也能立即释放 callback；`OnBrowserClosed()` 在释放 `browser_`、`client_` 前再次调用 `RejectAll()` 作为幂等兜底。把默认析构改为显式析构并调用 `RejectAll()`，使尚未响应的 callback 始终被释放。

- [ ] **Step 6: 编译 CEF widgets 和独立浏览器。**

  ```powershell
  cmake --build build\cef100-msvc2022-x64 --config Release `
    --target offscreen_cef_widgets offscreen_cef_browser
  ```

  Expected: 两个目标成功；`browser_client.h`、`browser_service.h` 的新签名一致，无 CEF callback 泄漏或重复 override 错误。

- [ ] **Step 7: 提交 CEF 内部下载决策链路。**

  ```powershell
  git add src\browser\browser_client.h src\browser\browser_client.cpp `
    src\browser\browser_service.h src\browser\browser_service.cpp
  git commit -m "feat: defer downloads for host confirmation"
  ```

### Task 3: 在 CefWebView 公开宿主确认 API

**Files:**
- Modify: `src/offscreen_cef/cef_web_view.h:1-119`
- Modify: `src/qt/cef_web_view.cpp:1-120,233-254`

**Interfaces:**
- Consumes: Task 2 的 BrowserService 下载决策接口。
- Produces: 宿主可用的 `SetDownloadDecisionMode()`、`AcceptDownload()`、`CancelDownload()` 和 `downloadRequested()`。

- [ ] **Step 1: 在公共头文件增加不含 CEF 类型的 API。**

  `cef_web_view.h` 包含 `offscreen_cef/download_types.h`，在 `public slots` 增加：

  ```cpp
  void SetDownloadDecisionMode(DownloadDecisionMode mode);
  void AcceptDownload(DownloadRequestId id, const QString& fullPath);
  void CancelDownload(DownloadRequestId id);
  ```

  在 `signals` 增加：

  ```cpp
  void downloadRequested(DownloadRequestId id,
                         const QString& suggestedFileName,
                         const QUrl& sourceUrl);
  ```

  保留现有 `SetDownloadDirectory()` 和 `downloadStateChanged()` 签名不变。

- [ ] **Step 2: 把 BrowserService 请求转换为 Qt 信号。**

  在 `CefWebView` 构造函数中设置：

  ```cpp
  browser_service_->SetDownloadRequestCallback(
      [this](DownloadRequestId id, const std::string& suggested_name,
             const std::string& source_url) {
        emit downloadRequested(
            id, QString::fromUtf8(suggested_name.data(),
                                  static_cast<int>(suggested_name.size())),
            Utf8ToUrl(source_url));
      });
  ```

- [ ] **Step 3: 实现宿主响应入口和路径校验。**

  `cef_web_view.cpp` 增加 `QDir`、`QFileInfo` include。实现：

  ```cpp
  void CefWebView::SetDownloadDecisionMode(DownloadDecisionMode mode) {
    if (browser_service_) {
      browser_service_->SetDownloadDecisionMode(mode);
    }
  }

  void CefWebView::AcceptDownload(DownloadRequestId id,
                                  const QString& fullPath) {
    const QString native_path = QDir::toNativeSeparators(fullPath);
    if (!browser_service_ || native_path.isEmpty() ||
        !QFileInfo(native_path).isAbsolute()) {
      CancelDownload(id);
      return;
    }
    browser_service_->AcceptDownload(id, native_path.toStdWString());
  }

  void CefWebView::CancelDownload(DownloadRequestId id) {
    if (browser_service_) {
      browser_service_->CancelDownload(id);
    }
  }
  ```

- [ ] **Step 4: 编译公开 API。**

  ```powershell
  cmake --build build\cef100-msvc2022-x64 --config Release `
    --target offscreen_cef_widgets embedding_demo_webview
  ```

  Expected: 公共头文件通过 AUTOMOC 和 C++ 编译；宿主不需要 include CEF 头文件即可使用新 API。

- [ ] **Step 5: 提交 WebView API。**

  ```powershell
  git add src\offscreen_cef\cef_web_view.h src\qt\cef_web_view.cpp
  git commit -m "feat: expose host download decisions in webview"
  ```

### Task 4: 让 CefTabbedBrowser 管理所有标签的下载配置与请求

**Files:**
- Modify: `src/offscreen_cef/cef_tabbed_browser.h:1-110`
- Modify: `src/qt/cef_tabbed_browser.cpp:27-89,140-198`

**Interfaces:**
- Consumes: Task 3 的 CefWebView 下载 API。
- Produces: 容器级下载目录/模式设置、所有标签请求信号和所有标签状态信号；现有 current-only 信号继续保留。

- [ ] **Step 1: 声明容器级设置和全标签信号。**

  `cef_tabbed_browser.h` 包含 `QString` 和 `offscreen_cef/download_types.h`。在 `public slots` 增加：

  ```cpp
  void SetDownloadDirectory(const QString& path);
  void SetDownloadDecisionMode(DownloadDecisionMode mode);
  ```

  在 `signals` 增加：

  ```cpp
  void downloadRequested(CefWebView* view,
                         DownloadRequestId id,
                         const QString& suggestedFileName,
                         const QUrl& sourceUrl);
  void downloadStateChanged(CefWebView* view,
                            int state,
                            const QString& fileName,
                            const QString& fullPath);
  ```

  增加成员：

  ```cpp
  QString download_directory_;
  DownloadDecisionMode download_decision_mode_ =
      DownloadDecisionMode::kAutomatic;
  ```

- [ ] **Step 2: 新标签在加载前继承容器配置。**

  在 `OpenTab()` 创建 `CefWebView` 后、调用 `LoadUrl()` 前执行：

  ```cpp
  view->SetDownloadDirectory(download_directory_);
  view->SetDownloadDecisionMode(download_decision_mode_);
  ```

  该位置同时覆盖显式 `OpenTab()` 和现有 `newWindowRequested -> OpenTab()` popup 路径。

- [ ] **Step 3: 转发所有标签请求和状态。**

  每个 view 的 `downloadRequested` 都无条件转发：

  ```cpp
  connect(view, &CefWebView::downloadRequested, this,
          [this, view](DownloadRequestId id,
                       const QString& suggested_name,
                       const QUrl& source_url) {
            emit downloadRequested(view, id, suggested_name, source_url);
          });
  ```

  每个 view 的 `downloadStateChanged` 先无条件发出新的全标签信号，再仅当 `currentWidget() == view` 时发出现有 `currentDownloadStateChanged`。不得删除现有 current-only 行为。

- [ ] **Step 4: 设置变更同时应用到现有标签。**

  两个 setter 先保存容器值，再遍历现有标签：

  ```cpp
  void CefTabbedBrowser::SetDownloadDirectory(const QString& path) {
    download_directory_ = path;
    for (int index = 0; index < tabs_->count(); ++index) {
      if (auto* view = qobject_cast<CefWebView*>(tabs_->widget(index))) {
        view->SetDownloadDirectory(download_directory_);
      }
    }
  }

  void CefTabbedBrowser::SetDownloadDecisionMode(
      DownloadDecisionMode mode) {
    download_decision_mode_ = mode;
    for (int index = 0; index < tabs_->count(); ++index) {
      if (auto* view = qobject_cast<CefWebView*>(tabs_->widget(index))) {
        view->SetDownloadDecisionMode(download_decision_mode_);
      }
    }
  }
  ```

  模式从 ask-host 切回 automatic 时，各 WebView 会按 Task 2 规则释放自己的未决 callback。

- [ ] **Step 5: 编译 Tabbed API。**

  ```powershell
  cmake --build build\cef100-msvc2022-x64 --config Release `
    --target offscreen_cef_widgets embedding_demo_tabbed_browser
  ```

  Expected: AUTOMOC 成功处理含 `CefWebView*` 和 `DownloadRequestId` 的信号；显式标签和 popup 创建路径均复用同一配置逻辑。

- [ ] **Step 6: 提交 Tabbed API。**

  ```powershell
  git add src\offscreen_cef\cef_tabbed_browser.h `
    src\qt\cef_tabbed_browser.cpp
  git commit -m "feat: route download decisions for all tabs"
  ```

### Task 5: 在两个 embedding demo 中演示异步宿主对话框

**Files:**
- Modify: `embedding_demo/webview/main.cpp:1-105`
- Modify: `embedding_demo/tabbed_browser/main.cpp:1-88`
- Create: `tests/download_confirmation_test.html`

**Interfaces:**
- Consumes: Task 3 和 Task 4 的公开 API。
- Produces: 可直接运行的宿主示例和无服务端依赖的本地下载触发页。

- [ ] **Step 1: 创建本地下载验收页。**

  `tests/download_confirmation_test.html` 包含三个按钮：下载 `alpha.txt`、下载 `beta.txt`、连续触发两次下载。使用 Blob URL 创建真实下载：

  ```html
  <!doctype html>
  <html lang="zh-CN">
  <meta charset="utf-8">
  <title>Download confirmation test</title>
  <button onclick="downloadFile('alpha.txt', 'alpha')">下载 alpha.txt</button>
  <button onclick="downloadFile('beta.txt', 'beta')">下载 beta.txt</button>
  <button onclick="downloadBoth()">连续下载两个文件</button>
  <script>
    function downloadFile(name, text) {
      const url = URL.createObjectURL(new Blob([text], {type: 'text/plain'}));
      const link = document.createElement('a');
      link.href = url;
      link.download = name;
      link.click();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
    }
    function downloadBoth() {
      downloadFile('alpha.txt', 'alpha');
      downloadFile('beta.txt', 'beta');
    }
  </script>
  </html>
  ```

- [ ] **Step 2: 在 WebView demo 中连接异步保存对话框。**

  在创建 view 后先连接 `downloadRequested`，再调用：

  ```cpp
  view_->SetDownloadDecisionMode(
      offscreen::DownloadDecisionMode::kAskHost);
  ```

  signal handler 创建 parent 为 `view_` 的 `QFileDialog`，使 WebView 销毁时对话框随之销毁，并设置：

  ```cpp
  dialog->setAcceptMode(QFileDialog::AcceptSave);
  dialog->setFileMode(QFileDialog::AnyFile);
  dialog->setConfirmOverwrite(true);
  dialog->setDirectory(
      QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
  dialog->selectFile(suggested_name);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  ```

  `fileSelected` 调用 `view_->AcceptDownload(id, path)`，`QDialog::rejected` 调用 `view_->CancelDownload(id)`，最后使用 `dialog->open()`。两个 lambda 的 context object 使用 `view_`，保证 view 销毁后自动断开。

- [ ] **Step 3: 在 Tabbed demo 中连接所有标签请求。**

  先连接 `CefTabbedBrowser::downloadRequested`，再设置容器模式为 `kAskHost`。handler 使用 signal 参数中的 `CefWebView* view`，用 `QPointer<CefWebView>` 守护；文件被选择时仅在 guard 非空时调用 `AcceptDownload()`，拒绝时调用 `CancelDownload()`。对话框 parent 为发起请求的 `view`，不得通过 `CurrentView()` 猜测下载来源。

- [ ] **Step 4: 让 Tabbed demo 接受本地 HTML 参数。**

  无参数时保留当前默认 URL；有一个参数时按以下逻辑打开本地页面，非法参数返回退出码 2：

  ```cpp
  QUrl initial_url(QStringLiteral("http://192.168.42.116"));
  if (argc > 1) {
    const QFileInfo html_file(QString::fromLocal8Bit(argv[1]));
    const QString suffix = html_file.suffix().toLower();
    if (!html_file.exists() || !html_file.isFile() ||
        (suffix != QStringLiteral("html") &&
         suffix != QStringLiteral("htm"))) {
      return 2;
    }
    initial_url = QUrl::fromLocalFile(html_file.absoluteFilePath());
  }
  window.OpenInitialTab(initial_url);
  ```

- [ ] **Step 5: 构建两个 demo。**

  ```powershell
  cmake --build build\cef100-msvc2022-x64 --config Release `
    --target embedding_demo_webview embedding_demo_tabbed_browser
  ```

  Expected: 两个 demo 均成功编译；没有同步文件对话框调用。

- [ ] **Step 6: 提交宿主示例和验收页。**

  ```powershell
  git add embedding_demo\webview\main.cpp `
    embedding_demo\tabbed_browser\main.cpp `
    tests\download_confirmation_test.html
  git commit -m "docs: demonstrate host download confirmation"
  ```

### Task 6: 更新嵌入接口文档

**Files:**
- Modify: `docs/embedding.md:79-151`
- Modify: `docs/scenarios.md`
- Modify: `README.md:52-71,113-125`

**Interfaces:**
- Consumes: Task 3–5 的最终公开签名和 demo 行为。
- Produces: 宿主开发者可直接复制的 WebView/Tabbed 下载接入说明。

- [ ] **Step 1: 在 `docs/embedding.md` 增加“宿主控制下载确认”章节。**

  文档必须明确：

  - 默认 `kAutomatic` 与现有行为相同。
  - 连接 `downloadRequested` 后再设置 `kAskHost`。
  - `AcceptDownload()` 参数是含文件名的绝对完整路径。
  - `CancelDownload()` 实际释放 before-download callback，不调用 CEF 中不存在的 cancel 方法。
  - 使用异步 `QFileDialog::open()`，不要使用同步静态函数。
  - Tabbed 使用信号提供的 `CefWebView*` 处理后台/popup 标签，不使用 `CurrentView()`。
  - 页面或标签关闭会使未决请求失效，宿主 lambda 应使用 Qt context object 或 `QPointer`。

  同时放入与 Task 5 一致的 WebView 和 Tabbed 最小代码片段，不使用未声明的类型或方法。

- [ ] **Step 2: 更新场景文档和 README 索引。**

  `docs/scenarios.md` 在 WebView/Tabbed 场景中各增加下载确认入口；README 只增加一段 API 摘要和 `docs/embedding.md` 链接，不复制完整教程。

- [ ] **Step 3: 检查文档中的接口名称。**

  ```powershell
  rg -n "DownloadDecisionMode|downloadRequested|AcceptDownload|CancelDownload" `
    README.md docs\embedding.md docs\scenarios.md `
    src\offscreen_cef src\qt
  ```

  Expected: 文档使用的四个接口名称与头文件完全一致；不得把 before-download callback 描述为具有取消成员函数。

- [ ] **Step 4: 提交文档。**

  ```powershell
  git add README.md docs\embedding.md docs\scenarios.md
  git commit -m "docs: document host-controlled downloads"
  ```

### Task 7: 完整自动化验证和 GUI 人工验收

**Files:**
- Verify only: `build/cef100-msvc2022-x64/`
- Verify only: `bin/msvc-2022-x64/Release/`
- Verify only: `tests/download_confirmation_test.html`

**Interfaces:**
- Consumes: Task 1–6 的实现。
- Produces: 分离记录的 CTest、构建、WebView GUI、Tabbed GUI 和进程退出结果。

- [ ] **Step 1: 运行格式和工作树边界检查。**

  ```powershell
  git diff --check
  git status --short
  ```

  Expected: `git diff --check` 无输出；`git status --short` 只显示本功能文件以及执行前已有的触摸计划文件，不包含 build/bin 产物。

- [ ] **Step 2: 重新配置并构建 Release。**

  ```powershell
  cmake -S . -B build\cef100-msvc2022-x64 `
    -G "Visual Studio 17 2022" -A x64 `
    -DCEF_ROOT="D:\Git\cef_binary_100.0.14+g4e5ba66+chromium-100.0.4896.75_windows64_minimal" `
    -DQt6_DIR="D:\IDE\QT6.9.3\6.9.3\msvc2022_64\lib\cmake\Qt6" `
    -DCEF_RUNTIME_LIBRARY_FLAG=/MD `
    -DOFFSCREEN_BUILD_APP=ON `
    -DOFFSCREEN_BUILD_TESTS=ON `
    -DOFFSCREEN_BUILD_EMBEDDING_DEMOS=ON
  cmake --build build\cef100-msvc2022-x64 --config Release
  ```

  Expected: `offscreen_cef_browser.exe`、subprocess、两个 demo 和全部测试目标构建成功。

- [ ] **Step 3: 运行全部 CTest。**

  ```powershell
  ctest --test-dir build\cef100-msvc2022-x64 -C Release `
    --output-on-failure
  ```

  Expected: 全部已注册测试通过，包含 `download_request_registry_tests`。

- [ ] **Step 4: 验证默认自动模式兼容。**

  运行未启用 `kAskHost` 的 `offscreen_cef_browser.exe`，打开下载测试页并点击 `alpha.txt`。确认不弹出宿主确认框，文件按原有逻辑进入系统下载目录或配置目录，状态栏仍显示开始/完成。

- [ ] **Step 5: 验证 WebView 接受和取消。**

  ```powershell
  & ".\bin\msvc-2022-x64\Release\embedding_demo_webview.exe" `
    ".\tests\download_confirmation_test.html"
  ```

  依次确认：点击下载后先出现保存对话框且文件尚未落盘；选择不同目录和文件名后文件内容正确；取消后不生成文件；取消后再次下载仍可成功。

- [ ] **Step 6: 验证并发请求。**

  点击“连续下载两个文件”，确认两个请求互不覆盖；分别接受一个、取消一个，最终只存在被接受的文件。再次操作时不存在旧 request ID 误响应。

- [ ] **Step 7: 验证 Tabbed 后台和 popup 标签。**

  ```powershell
  & ".\bin\msvc-2022-x64\Release\embedding_demo_tabbed_browser.exe" `
    ".\tests\download_confirmation_test.html"
  ```

  在两个标签分别触发下载并切换当前标签，确认每个对话框仍作用于发起下载的 view；popup 创建的新标签继承确认模式；后台标签的请求不会因 current-only 过滤而丢失。

- [ ] **Step 8: 验证待确认状态下关闭。**

  在保存对话框打开时关闭对应 WebView/标签，再关闭宿主。确认没有崩溃、死锁或残留 `embedding_demo_*.exe`、`offscreen_cef_browser.exe`、`offscreen_cef_subprocess.exe` 进程；该项必须记录为 GUI 人工验收，不能由 CTest 代替。

- [ ] **Step 9: 最终提交前复核。**

  ```powershell
  git diff --check
  git status --short
  git log --oneline -6
  ```

  Expected: 本功能提交按 Task 1–6 分离；工作树没有意外构建产物；原有未跟踪触摸计划仍保持原状。

## 执行顺序与停止条件

1. 严格按 Task 1 → Task 7 执行；Task 1 的注册表测试未通过时不得接入 CEF callback。
2. 若当前 `cef-100` 工作树出现与计划文件重叠的用户修改，停止并先与用户确认，不覆盖。
3. 若 CEF 100 实际头文件与本计划不同，以 `D:\Git\cef_binary_100.0.14+g4e5ba66+chromium-100.0.4896.75_windows64_minimal\include\cef_download_handler.h` 为准，并更新计划后再继续。
4. 若 build/CTest 通过但 GUI 对话框、真实文件或关闭流程未验证，只报告静态/构建/CTest 通过，不报告下载功能验收完成。
