# Offscreen CEF Browser

基于 Chromium Embedded Framework (CEF 96) + Qt 5.14.2 的离屏渲染浏览器，支持多 Tab 页浏览。

当前已完成 V2.5.1，实现了完整的 OSR 渲染、输入事件转发、中文输入法支持及多 Tab 页管理。

## 版本历史

| 版本 | 提交 | 内容 |
| --- | --- | --- |
| V1 | `40ae5ef` | 工程骨架：CMake 构建系统、CEF/Qt 工具链校验、`offscreen_core` 静态库、7 个单元测试 |
| V2.1 | `24a593c` | 浏览器核心：`BrowserApp`/`BrowserClient`/`BrowserService`、CEF 初始化与生命周期、构建脚本 `scripts/build.ps1` |
| V2.2 | `7c791c5` | 软件 OSR 渲染：`OsrRenderHandler::OnPaint` BGRA buffer → `BrowserFrame` → `BrowserWidget` Qt 绘制 |
| V2.3 | `1fbb004` | 输入事件转发：鼠标/键盘/滚轮/焦点/光标事件从 Qt 到 CEF 的完整映射 |
| V2.4 | `0773060` | IME 中文输入法支持：`BrowserImeCore` + `BrowserImeHandler`，WM_IME_* 消息处理与 CEF IME API 对接 |
| V2.5.1 | `250ed5c` | 子窗口创建与 Tab 页管理：`TabManager` 支持多 Web 页以 Tab 形式同时浏览 |

## 构建

**前置条件：** CMake 3.21+、VS2017 x64、CEF 96 (`windows64_vs2017`)、Qt 5.14.2 (`msvc2017_64`)。

```powershell
PowerShell -ExecutionPolicy Bypass -File "scripts\build.ps1"
```

脚本按顺序执行：工具链校验 → CMake 配置 → 构建 → CTest 测试。

可选参数：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `-CefRoot` | `D:\Git\cef_binary_96.0.18+gfe551e4+chromium-96.0.4664.110_windows64_vs2017` | CEF binary distribution 路径 |
| `-QtPrefix` | 自动从 PATH 查找 `qmake` | Qt 5.14.2 msvc2017_64 前缀 |
| `-Configuration` | `Debug` | `Debug` 或 `Release` |

## 架构

两个可执行文件共享一个静态库：

| 目标 | 角色 |
| --- | --- |
| `offscreen_cef_browser` | Qt GUI + CEF 浏览器进程，链接 `offscreen_core`、`Qt5::Widgets`、`libcef_lib`、`libcef_dll_wrapper` |
| `offscreen_cef_subprocess` | CEF 子进程入口，仅链接 CEF 库 |
| `offscreen_core` (静态库) | 共享逻辑：`AppConfig`、输入映射、IME 核心、几何、绘制几何、关闭状态、窗口标题，无 Qt/CEF 依赖 |

### 启动链路

`main()` → `CefExecuteProcess` → `CefInitialize` (external message pump) → `QApplication` → 10ms `CefDoMessageLoopWork` 定时器 → `BrowserService::CreateBrowser` (Alloy style, `SetAsWindowless`) → `QApplication::exec()` → 关闭 → `CefShutdown`

### OSR 渲染链路

`OsrRenderHandler::OnPaint` (BGRA buffer) → `BrowserFrame::SetViewImage` (互斥锁下深拷贝) → `PaintUpdateCallback` → `BrowserWidget::ScheduleFrameUpdate` (Qt::QueuedConnection) → `paintEvent` → `QPainter::drawImage`。弹窗图像通过 `OnPopupShow/OnPopupSize` 叠加合成。

### 输入链路

`BrowserWidget::nativeEvent` (WM_KEYDOWN/KEYUP/CHAR/SYSCHAR) + Qt 鼠标/键盘/滚轮/焦点事件重写 → `browser_input_mapping` 辅助函数 (Qt→CEF 标志转换) → `BrowserService::Send*` 方法 → `CefBrowserHost::SendKeyEvent/SendMouseClickEvent/...`

### 生命周期

`BrowserClient` 实现 `CefClient` + 6 个 handler 接口，回调委托给 `BrowserService`（实现 `BrowserClient::Delegate`）。关闭使用 `DecideBrowserCloseAction` 状态机依次执行 `TryCloseBrowser` → `DoClose` → `OnBeforeClose`。

### 消息循环

External pump 模式：`BrowserApp::OnScheduleMessagePumpWork` → `QTimer::singleShot(delay_ms)` → `CefDoMessageLoopWork()`。CEF UI 线程 = Qt GUI 线程。

## 目录结构

```
offscreen-cef-browser/
  CMakeLists.txt              # 根构建文件
  README.md
  scripts/
    build.ps1                 # 一键构建脚本
  src/
    app/                      # 入口：main.cpp, BrowserApp, AppConfig, window_title
    browser/                  # 核心：BrowserService, BrowserClient, OsrRenderHandler,
                              #       TabManager, BrowserImeCore/Handler,
                              #       BrowserFrame, input_mapping, geometry, close_state
    qt/                       # Qt 集成：BrowserWidget (绘制、输入、IME、nativeEvent)
    subprocess/               # CEF 子进程入口
  tests/                      # 7 个测试可执行文件 + test_input_page.html
  docs/                       # 调研、架构、实现计划
```

## 关键约束

- Debug 构建必须定义 `_HAS_ITERATOR_DEBUGGING=0`，避免 Qt Debug DLL 与 MSVC 迭代器调试的 ABI 不兼容（否则 `QString::toStdString()` 崩溃）。
- CEF 96 + Alloy style 是 OSR/windowless 模式的硬性要求。浏览器创建时设置 `windowless_frame_rate=30`。
- DPI 坐标规则：`GetViewRect` 返回 DIP；`OnPaint` buffer 尺寸是物理像素；`device_scale_factor` 必须等于 `devicePixelRatioF()`；脏矩形需从物理像素转 DIP 再调用 `QWidget::update()`。
- `BrowserFrame` 使用 `std::mutex` —— `Snapshot()` 返回深拷贝供 Qt 线程使用。
- 测试仅链接 `offscreen_core`（无 Qt/CEF 依赖），通过 CTest 运行。

## 文档索引

- [docs/README.md](docs/README.md) — 文档总览与阅读顺序
- [docs/research.md](docs/research.md) — CEF、cefclient、Qt 前期调研
- [docs/architecture.md](docs/architecture.md) — 架构、线程模型、渲染与输入链路
- [docs/implementation-plan.md](docs/implementation-plan.md) — 分阶段开发计划与验收标准
- [docs/visualization-system-compatibility.md](docs/visualization-system-compatibility.md) — 可视化设计系统兼容性要求

## 技术栈

| 组件 | 版本 | 说明 |
| --- | --- | --- |
| CEF | 96.0.18 (Chromium 96) | `windows64_vs2017`，Alloy style，OSR windowless |
| Qt | 5.14.2 | `msvc2017_64`，Widgets 模块 |
| 编译器 | MSVC 2017 (19.1x) | x64，`/MT` 或 `/MTd` 静态运行时 |
| CMake | 3.21+ | Visual Studio 15 2017 generator |
| C++ 标准 | C++17 | |

## 主要参考资料

- [CEF General Usage](https://chromiumembedded.github.io/cef/general_usage.html)
- [CEF Branches and Building](https://chromiumembedded.github.io/cef/branches_and_building.html)
- [CEF cefclient OSR 参考](https://github.com/chromiumembedded/cef/tree/master/tests/cefclient)
- [CEF Render Handler](https://raw.githubusercontent.com/chromiumembedded/cef/master/include/cef_render_handler.h)
