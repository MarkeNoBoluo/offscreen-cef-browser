# Offscreen CEF Browser

基于 Chromium Embedded Framework (CEF 100) + Qt 6.9.3 的 Windows x64 离屏渲染浏览器，支持多 Tab 页浏览，也可作为模块嵌入其他 Qt GUI 程序。

项目已实现完整的 OSR 渲染、输入事件转发、中文输入法支持及多 Tab 页管理，并提供 `CefRuntime`、`CefWebView` 与 `CefTabbedBrowser`，用于复用浏览器运行时、单页 WebView 和多 Tab 容器。`cef-100` 分支已通过 Qt 6.9.3 + MSVC 2022 x64 Release 编译、7/7 CTest 和本地 HTML Web 操作人工验证。

## 版本历史

| 版本 | 提交 | 内容 |
| --- | --- | --- |
| V1 | `40ae5ef` | 工程骨架：CMake 构建系统、CEF/Qt 工具链校验、`offscreen_core` 静态库、7 个单元测试 |
| V2.1 | `24a593c` | 浏览器核心：`BrowserApp`/`BrowserClient`/`BrowserService`、CEF 初始化与生命周期、构建脚本 `scripts/build.ps1` |
| V2.2 | `7c791c5` | 软件 OSR 渲染：`OsrRenderHandler::OnPaint` BGRA buffer → `BrowserFrame` → `BrowserWidget` Qt 绘制 |
| V2.3 | `1fbb004` | 输入事件转发：鼠标/键盘/滚轮/焦点/光标事件从 Qt 到 CEF 的完整映射 |
| V2.4 | `0773060` | IME 中文输入法支持：`BrowserImeCore` + `BrowserImeHandler`，WM_IME_* 消息处理与 CEF IME API 对接 |
| V2.5.1 | `250ed5c` | 子窗口创建与 Tab 页管理：`TabManager` 支持多 Web 页以 Tab 形式同时浏览 |
| CEF 100 | `7315b0e` | 迁移至 CEF 100、Qt 6.9.3、VS2022 x64 和 `/MD`，适配 Qt6 原生事件及滚轮接口，WebView Demo 支持本地 HTML 参数 |

## 构建

**前置条件：** CMake 3.21+、Visual Studio 2022、CEF `100.0.14+g4e5ba66+chromium-100.0.4896.75_windows64_minimal` 和 Qt 6.9.3 `msvc2022_64`。当前配置仅支持 Windows x64。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\scripts\build.ps1" `
  -Architecture x64 `
  -CefRoot "D:\Git\cef_binary_100.0.14+g4e5ba66+chromium-100.0.4896.75_windows64_minimal" `
  -QtPrefix "D:\IDE\QT6.9.3\6.9.3\msvc2022_64" `
  -BuildDir "build\cef100-msvc2022-x64" `
  -Configuration Release
```

脚本按顺序执行：工具链校验 → CMake 配置 → 构建 → CTest 测试。

可选参数：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `-Architecture` | `x64` | 当前仅支持 `x64` |
| `-CefRoot` | CEF 100 `windows64_minimal` 默认路径 | CEF binary distribution 路径，必须是 CEF 100 x64 minimal 包 |
| `-QtPrefix` | 自动从 PATH 查找 `qmake` | 必须指向 Qt 6.9.3 `msvc2022_64` |
| `-BuildDir` | `build\cef100-msvc2022-x64` | CMake 构建目录 |
| `-Configuration` | `Release` | `Debug` 或 `Release` |

Release 产物位于 `bin\msvc-2022-x64\Release`。已验证生成以下目标：

- `offscreen_cef_browser.exe`
- `offscreen_cef_subprocess.exe`
- `embedding_demo_webview.exe`
- `embedding_demo_tabbed_browser.exe`

## 本地 HTML WebView 验证

`embedding_demo_webview.exe` 的第一个参数可以是本地 `.html` 或 `.htm` 文件的相对路径或绝对路径；路径不存在或扩展名无效时返回退出码 2。无参数时仍加载 Demo 的默认 HTTP 地址。

```powershell
$env:Path = "D:\IDE\QT6.9.3\6.9.3\msvc2022_64\bin;$env:Path"
$env:QT_PLUGIN_PATH = "D:\IDE\QT6.9.3\6.9.3\msvc2022_64\plugins"

& ".\bin\msvc-2022-x64\Release\embedding_demo_webview.exe" `
  ".\tests\test_input_page.html"
```

该入口已完成人工 Web 操作验证。

## 架构

浏览器演示程序、CEF 子进程和可复用静态库的职责如下：

| 目标 | 角色 |
| --- | --- |
| `offscreen_cef_browser` | 现有 Qt 浏览器演示程序，也是 CEF browser process |
| `offscreen_cef_subprocess` | CEF renderer/GPU/utility 子进程入口 |
| `offscreen_cef_runtime` (静态库) | `CefRuntime`、`BrowserApp`、CEF 初始化、external message pump、IME 原生消息路由 |
| `offscreen_cef_widgets` (静态库) | `CefWebView`、`CefTabbedBrowser`、OSR 绘制、输入、IME、浏览器与 Tab 管理 |
| `offscreen_core` (静态库) | 共享逻辑：`AppConfig`、输入映射、IME 核心、几何、绘制几何、关闭状态、窗口标题，无 Qt/CEF 依赖 |

宿主程序通过 `offscreen_cef::widgets` 链接组件，并调用 `offscreen_cef_deploy(host_app)` 部署 CEF runtime、resources 和 `offscreen_cef_subprocess.exe`。完整示例见 [docs/embedding.md](docs/embedding.md)。

### 启动链路

`CefRuntime::ExecuteSubprocess` → `QApplication` → `CefRuntime::Initialize` (external message pump) → `CefBrowserHost::CreateBrowser` (Alloy style, `SetAsWindowless`) → `QApplication::exec()` → 所有 browser 关闭 → `CefRuntime::Shutdown`

### OSR 渲染链路

`OsrRenderHandler::OnPaint` (BGRA buffer) → `BrowserFrame::SetViewImage` (互斥锁下深拷贝) → `PaintUpdateCallback` → `BrowserWidget::ScheduleFrameUpdate` (Qt::QueuedConnection) → `paintEvent` → `QPainter::drawImage`。弹窗图像通过 `OnPopupShow/OnPopupSize` 叠加合成。

### 输入链路

`BrowserWidget::nativeEvent` (WM_KEYDOWN/KEYUP/CHAR/SYSCHAR) + Qt 鼠标/键盘/滚轮/焦点事件重写 → `browser_input_mapping` 辅助函数 (Qt→CEF 标志转换) → `BrowserService::Send*` 方法 → `CefBrowserHost::SendKeyEvent/SendMouseClickEvent/...`

### 生命周期

`BrowserClient` 实现 `CefClient` + 6 个 handler 接口，回调委托给 `BrowserService`（实现 `BrowserClient::Delegate`）。关闭使用 `DecideBrowserCloseAction` 状态机依次执行 `TryCloseBrowser` → `DoClose` → `OnBeforeClose`。

### 消息循环

External pump 模式：`BrowserApp::OnScheduleMessagePumpWork` → `CefMessagePumpScheduler` → 单次 `QTimer` → `CefDoMessageLoopWork()`；另有 10 ms fallback timer 保证消息泵活性。CEF UI 线程 = Qt GUI 线程。

## 目录结构

```
offscreen-cef-browser/
  CMakeLists.txt              # 根构建文件
  README.md
  scripts/
    build.ps1                 # 一键构建脚本
  embedding_demo/
    webview/                  # 单页 CefWebView 示例，支持本地 HTML 参数
    tabbed_browser/           # 多 Tab CefTabbedBrowser 示例
  src/
    app/                      # 入口：main.cpp, BrowserApp, AppConfig, window_title
    browser/                  # 核心：BrowserService, BrowserClient, OsrRenderHandler,
                              #       TabManager, BrowserImeCore/Handler,
                              #       BrowserFrame, input_mapping, geometry, close_state
    offscreen_cef/            # 宿主公开 API：CefRuntime, CefWebView, CefTabbedBrowser
    qt/                       # Qt 集成：BrowserWidget、CefWebView、CefTabbedBrowser
    runtime/                  # 进程级 CEF 生命周期与 IME 原生消息路由
    subprocess/               # CEF 子进程入口
  tests/                      # 7 个测试可执行文件 + test_input_page.html
  docs/                       # 调研、架构、实现计划
```

## 关键约束

- Debug 构建必须定义 `_HAS_ITERATOR_DEBUGGING=0`，避免 Qt Debug DLL 与 MSVC 迭代器调试的 ABI 不兼容（否则 `QString::toStdString()` 崩溃）。
- 当前分支固定使用 CEF 100 `windows64_minimal`、Qt 6.9.3 `msvc2022_64` 和 VS2022 x64，不支持 Win32。
- Qt 与 CEF wrapper 统一使用 `/MD` 或 `/MDd` 动态 MSVC 运行库；CEF 配置前必须设置 `CEF_RUNTIME_LIBRARY_FLAG=/MD`。
- CEF 100 + Alloy style 是 OSR/windowless 模式的硬性要求。浏览器创建时设置 `windowless_frame_rate=30`。
- DPI 坐标规则：`GetViewRect` 返回 DIP；`OnPaint` buffer 尺寸是物理像素；`device_scale_factor` 必须等于 `devicePixelRatioF()`；脏矩形需从物理像素转 DIP 再调用 `QWidget::update()`。
- `BrowserFrame` 使用 `std::mutex` —— `Snapshot()` 返回深拷贝供 Qt 线程使用。
- 一个宿主进程只能创建一个 `CefRuntime`；必须在 `QApplication` 前执行 `CefRuntime::ExecuteSubprocess()`。
- 关闭时先关闭全部 `CefWebView`，确认 `CefRuntime::AllBrowsersClosed()` 后才能调用 `CefRuntime::Shutdown()`。
- 测试仅链接 `offscreen_core`（无 Qt/CEF 依赖），通过 CTest 运行。

## 文档索引

- [docs/README.md](docs/README.md) — 文档总览与阅读顺序
- [docs/research.md](docs/research.md) — CEF、cefclient、Qt 前期调研
- [docs/architecture.md](docs/architecture.md) — 架构、线程模型、渲染与输入链路
- [docs/implementation-plan.md](docs/implementation-plan.md) — 分阶段开发计划与验收标准
- [docs/visualization-system-compatibility.md](docs/visualization-system-compatibility.md) — 可视化设计系统兼容性要求
- [docs/embedding.md](docs/embedding.md) — 将 Runtime、单页 WebView 或多 Tab 组件嵌入宿主 Qt 程序

## 技术栈

| 组件 | 版本 | 说明 |
| --- | --- | --- |
| CEF | 100.0.14+g4e5ba66+chromium-100.0.4896.75 | `windows64_minimal`；Alloy style，OSR windowless |
| Qt | 6.9.3 | `msvc2022_64`；Widgets 模块 |
| 编译器 | MSVC 2022 19.44.35222.0 | x64；Qt 与 CEF wrapper 使用 `/MD` 或 `/MDd` |
| Windows SDK | 10.0.26100.0 | 实际 Release 构建验证版本 |
| CMake | 3.21+ | 已使用 3.31.10 和 Visual Studio 17 2022 generator 验证 |
| C++ 标准 | C++17 | |

## 主要参考资料

- [CEF General Usage](https://chromiumembedded.github.io/cef/general_usage.html)
- [CEF Branches and Building](https://chromiumembedded.github.io/cef/branches_and_building.html)
- [CEF cefclient OSR 参考](https://github.com/chromiumembedded/cef/tree/master/tests/cefclient)
- [CEF Render Handler](https://raw.githubusercontent.com/chromiumembedded/cef/master/include/cef_render_handler.h)
