# Offscreen CEF Browser

基于 Chromium Embedded Framework (CEF) 的离屏渲染浏览器项目规划仓库。目标是在 C++ 中集成 CEF，并使用 Qt Widgets 承载浏览器画面、输入事件、生命周期和调试能力。V1 主要支持 Qt 5.14.2 MSVC2017 64bit 环境。

当前仓库处于前期调研与设计阶段，尚未包含可编译源码。根目录 README 提供项目入口，完整调研与落地方案放在 [docs](docs/README.md)。

## 项目目标

- 使用 CEF 创建 windowless/off-screen browser。
- 使用 Qt 5.14.2 MSVC2017 64bit 承载渲染结果，首版优先采用 CPU buffer + QWidget/QPainter 路线。
- 后续按性能需求扩展到 QOpenGLWidget 纹理上传或 CEF shared texture 加速路径。
- 保留浏览器基础能力：导航、加载状态、鼠标/键盘/滚轮输入、中文输入法、弹窗、DevTools、下载和基础网络拦截。
- 兼容可视化设计系统运行要求：JavaScript、LocalStorage、HTML5 Video、Canvas、CSS3、XHR/Fetch、Cookie、WebSocket、Drag & Drop、Clipboard、File API 等能力必须可用。
- 构建系统采用 CMake，优先对齐 CEF binary distribution 和 Qt 官方 CMake 用法。
- 版本路线：V1 先实现 Windows x64（`windows64`/`win64`）+ Qt 5.14.2 MSVC2017 64bit；V2 再根据情况评估 Qt 6.9.3、`win32` 和 `linux-amd64`；不考虑 macOS。

## 已确认的关键技术决策

| 主题 | 决策 |
| --- | --- |
| Chromium 内核 | 最低按 Chromium 90+，推荐 Chromium 100+，最佳 Chromium 120+；当前 CEF stable 150 已满足该目标。 |
| CEF 版本 | 面向生产优先选择 CEF release branch，不直接依赖 master。2026-07-16 调研时官方 supported table 显示 Stable 为 150 / branch 7871。 |
| 运行风格 | OSR/windowless 浏览器需要使用 Alloy style；参考 cefclient 的 `--off-screen-rendering-enabled` 模式。 |
| 首版渲染 | 先实现 `CefRenderHandler::OnPaint` 的 BGRA buffer 拷贝，Qt 侧用 `QImage::Format_ARGB32` 与 `QPainter` 绘制。 |
| 加速渲染 | `OnAcceleratedPaint` / shared texture 作为二阶段优化，不阻塞首版，因为跨 CEF/Qt/OpenGL/D3D 的零拷贝互操作复杂度更高。 |
| 消息循环 | 推荐使用 CEF external message pump + Qt `QTimer` 集成；Windows/Linux 可评估 CEF multi-threaded message loop。 |
| DPI | `GetScreenInfo().device_scale_factor` 与 Qt `devicePixelRatioF()` 必须统一，`GetViewRect` 使用 DIP，`OnPaint` buffer 使用实际像素尺寸。 |
| 输入法 | 中文输入需要纳入首版验收，Qt `QInputMethodEvent` 要转发到 CEF IME API。 |
| 版本路线 | V1 仅要求 Windows x64（`windows64`/`win64`）+ Qt 5.14.2 MSVC2017 64bit 可构建、可运行、可部署；V2 再根据情况评估 Qt 6.9.3、`win32` 和 `linux-amd64`。 |

## 构建
	PowerShell -ExecutionPolicy Bypass -File "scripts\build.ps1"
## 文档索引

- [docs/README.md](docs/README.md)：文档总览与阅读顺序。
- [docs/research.md](docs/research.md)：CEF、cefclient、Qt 5.14.2 与后续 Qt6 评估方向的前期调研。
- [docs/visualization-system-compatibility.md](docs/visualization-system-compatibility.md)：可视化设计系统加载兼容性要求。
- [docs/architecture.md](docs/architecture.md)：目标架构、线程模型、渲染与输入链路。
- [docs/implementation-plan.md](docs/implementation-plan.md)：分阶段开发计划、验收标准、风险清单。

## 建议目录结构

```text
offscreen-cef-browser/
  CMakeLists.txt
  cmake/
  third_party/cef/
  src/
    app/
    browser/
    qt/
    subprocess/
  resources/
  docs/
```

说明：

- `third_party/cef/` 放 CEF binary distribution 或由本地路径变量指向，不建议把大型二进制直接提交到普通源码仓库。
- `src/subprocess/` 推荐单独生成 CEF 子进程可执行文件，便于控制进程启动成本和部署结构。
- `src/qt/` 承载 Qt widget、事件映射、DPI/IME 适配。
- `src/browser/` 承载 CEF client、render handler、生命周期和浏览器服务封装。

## 开发前置条件

- C++17 或更高，V1 主要面向 Qt 5.14.2 MSVC2017 64bit。
- CMake，Qt 5.14.2 MSVC2017 64bit。
- CEF binary distribution，优先选当前 supported stable branch。
- V1 使用 Windows x64（`windows64`/`win64`）工具链，主要支持 MSVC2017 64-bit，并确保 CEF、Qt、编译器和生成器架构一致。
- V2 再根据项目需要评估 Qt 6.9.3、`win32` 与 `linux-amd64`；如支持 `win32`，需要 x86 CEF/Qt/编译器一致；如支持 `linux-amd64`，按 CEF 当前 release branch 的 Linux 构建与运行时依赖准备工具链。

## 第一阶段验收目标

- 能启动 Qt 主窗口并初始化 CEF。
- 能创建一个 windowless browser 并加载 URL。
- `OnPaint` buffer 能稳定显示到 Qt widget。
- resize、鼠标、滚轮、键盘、焦点、基础中文输入可用。
- 可视化设计系统的 JS、LocalStorage、Canvas、Video、Fetch、Cookie、WebSocket 等能力通过验收。
- DevTools 可通过独立窗口或 remote debugging port 打开。
- 关闭窗口时能按 CEF 生命周期正确释放 browser 并调用 `CefShutdown()`。
- V1 在 Windows x64（`windows64`/`win64`）+ Qt 5.14.2 MSVC2017 64bit 环境完成构建、启动、渲染、输入和退出验收。

## 主要参考资料

- CEF General Usage: https://chromiumembedded.github.io/cef/general_usage.html
- CEF Branches and Building: https://chromiumembedded.github.io/cef/branches_and_building.html
- CEF cefclient sample: https://github.com/chromiumembedded/cef/tree/master/tests/cefclient
- CEF render handler header: https://raw.githubusercontent.com/chromiumembedded/cef/master/include/cef_render_handler.h
- Qt QWidget: https://doc.qt.io/qt-6/qwidget.html
- Qt QImage: https://doc.qt.io/qt-6/qimage.html
- Qt QOpenGLWidget: https://doc.qt.io/qt-6/qopenglwidget.html
- Qt CMake: https://doc.qt.io/qt-6/cmake-get-started.html
