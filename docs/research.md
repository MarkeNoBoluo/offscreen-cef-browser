# 前期调研

调研日期：2026-07-16

目标：为 C++ + CEF + Qt Widgets 的离屏渲染浏览器项目建立实现依据，重点确认 CEF OSR 机制、cefclient 可复用经验、Qt 渲染承载方案和版本选择。V1 主要支持 Qt 5.14.2 MSVC2017 64bit，V2 再根据情况评估 Qt 6.9.3。

## 资料来源

优先级按官方资料和源代码排列：

- CEF Documentation: https://chromiumembedded.github.io/cef/
- CEF General Usage: https://chromiumembedded.github.io/cef/general_usage.html
- CEF Branches and Building: https://chromiumembedded.github.io/cef/branches_and_building.html
- CEF cefclient sample: https://github.com/chromiumembedded/cef/tree/master/tests/cefclient
- cefclient browser directory: https://github.com/chromiumembedded/cef/tree/master/tests/cefclient/browser
- Doogie Qt + CEF browser: https://github.com/cretz/doogie
- `CefRenderHandler`: https://raw.githubusercontent.com/chromiumembedded/cef/master/include/cef_render_handler.h
- Qt QWidget: https://doc.qt.io/qt-6/qwidget.html
- Qt QImage: https://doc.qt.io/qt-6/qimage.html
- Qt QOpenGLWidget: https://doc.qt.io/qt-6/qopenglwidget.html
- Qt6 CMake: https://doc.qt.io/qt-6/cmake-get-started.html
- Qt5 CMake archive: https://doc.qt.io/archives/qt-5.15/cmake-get-started.html

补充说明：已尝试用 `chub.cmd search cef --json` 和 `chub.cmd search qt --json` 检索 API 文档。CEF 仅返回无关的 Azure Service Fabric 条目，Qt 无结果，因此未使用 chub 文档。

## CEF 总体结论

CEF 面向在第三方应用中嵌入 Chromium。官方 General Usage 明确列出离屏渲染是典型使用场景之一，并说明 CEF3 基于 Chromium Content API 的多进程模型。

需要关注的基础约束：

- browser process 通常就是宿主应用进程，负责窗口、UI、网络和多数应用逻辑。
- renderer process 负责 Blink 渲染和 V8 JavaScript。
- GPU process、utility process 等由 Chromium 按需创建。
- `CefExecuteProcess()` 必须在主进程初始化逻辑前执行，用于识别并运行子进程逻辑。
- `CefInitialize()`、消息循环、`CefShutdown()` 的调用顺序必须稳定。
- CEF API 大量回调有线程约束，特别是 UI thread、IO thread、renderer thread。

## CEF 版本选择

官方 Branches and Building 文档说明，production 应选择 release branch，而不是 master。原因包括：binary build 绑定具体 Chromium release、release branch 更适合发布、API 相对冻结。

2026-07-16 调研时，官方 supported table 显示：

| Channel | Release branch | Version | Branch date |
| --- | --- | --- | --- |
| Beta | 7922 | 151 | Jul 2026 |
| Stable | 7871 | 150 | Jun 2026 |
| LTS | 7559 | 144 | Dec 2025 |

建议：

- 默认选择 Stable 150 / branch 7871 作为首版开发基线。
- 需要长期维护时评估 LTS 144，但要接受 Web 平台能力滞后。
- 不建议直接跟 master，因为 master 跟随 Chromium master，官方明确不推荐生产使用。

## 平台支持策略

平台路线分两版推进：

| 版本 | 平台 | 策略 |
| --- | --- | --- |
| V1 | Windows x64 (`windows64`/`win64`) + Qt 5.14.2 MSVC2017 64bit | 首个可用版本只要求 Windows 64-bit 与 Qt 5.14.2 MSVC2017 64bit。CEF binary distribution、Qt、MSVC2017 工具链和 CMake 生成器必须全部使用 x64。 |
| V2 候选 | Qt 6.9.3 | 在 V1 功能稳定后评估是否支持 Qt 6.9.3，重点确认 CMake、Widgets API、OpenGL/RHI、部署结构和运行时 DLL 差异。 |
| V2 候选 | `win32` | 如需要 Windows 32-bit，再增加构建。CEF binary distribution、Qt、编译器和部署目录必须全部使用 x86，不能混用 x64 库。 |
| V2 候选 | `linux-amd64` | 如需要 Linux x86_64，再增加构建。重点处理 `libcef.so`、rpath、`chrome-sandbox` 权限策略、Qt platform plugin 和系统运行时依赖。 |

当前不考虑 macOS 平台，不进入构建、部署或验收范围。

## cefclient 参考价值

用户指定的参考项目是 `tests/cefclient`。该项目是 CEF 的完整示例应用，包含普通窗口、Views、OSR、DevTools、下载、IPC、scheme handler、测试菜单等大量集成模式。

重点参考文件：

| 文件/目录 | 用途 |
| --- | --- |
| `tests/cefclient/README.md` | cefclient 功能概览，说明 OSR、Alloy style、multi-threaded message loop 等运行模式。 |
| `browser/client_handler_osr.*` | OSR handler，将 `CefRenderHandler` 回调转发到 OSR delegate。 |
| `browser/browser_window_osr_win.cc` | Windows OSR browser 创建逻辑，包含 `SetAsWindowless` 和 Alloy style 约束。 |
| `browser/osr_render_handler_win.*` | OSR render handler 与 begin frame 触发逻辑。 |
| `browser/osr_renderer.*` | OpenGL 渲染器，适合后续 QOpenGLWidget 或 GPU 路线借鉴。 |
| `browser/osr_ime_handler_win.*` | Windows IME 处理，可作为中文输入实现参考。 |
| `browser/osr_dragdrop_win.*` | 拖拽处理参考。 |

cefclient 不应整包照搬。建议抽取其结构思路：browser 生命周期、OSR delegate、输入事件转换、popup/IME/dragdrop 的处理边界。

## Doogie 参考价值

用户补充的参考项目 `cretz/doogie` 是 MIT 许可的 Qt + CEF 浏览器。它不是 OSR/windowless 渲染路线，而是通过 `CefWindowInfo::SetAsChild(...)` 把 CEF 原生窗口嵌入 Qt widget，因此不能作为 `CefRenderHandler::OnPaint`、popup buffer 合成、DPI buffer 映射或 OSR IME 的直接实现样板。

仍然有参考价值的部分：

| 文件/目录 | 可参考点 |
| --- | --- |
| `src/cef/cef.*` | `CefExecuteProcess`、`CefInitialize`、`CefDoMessageLoopWork`、`CefShutdown` 的 Qt 应用封装方式。 |
| `src/cef/cef_handler.*` | 用一个 `CefClient` 聚合 context menu、display、download、focus、keyboard、lifespan、load、request 等 handler，并用 Qt signal 向 UI 层转发。 |
| `src/cef/cef_widget.*` | 导航、DevTools、下载、favicon、fullscreen、认证弹窗、load/error 状态与 Qt widget 的组织方式。 |
| `src/profile.*` | `cache_path`、`user_data_path`、remote debugging、profile 持久化与 request context 设置。 |
| `src/build.go`、`src/cef/cef.pri` | Windows/Linux 的 CEF wrapper 构建、运行时资源复制、Qt plugin、CEF resources/locales 部署清单。 |

需要谨慎对待的差异：

- Doogie 使用 Qt5 + qmake + Go build script，本项目计划使用 CMake，不建议照搬构建系统。
- Doogie 的 Windows 构建脚本固定 64-bit，Linux 也只支持 amd64；可验证 V1 Windows x64 与 V2 候选 Linux amd64 的部分工程化方向，但不能直接覆盖 V2 候选 `win32`。
- Doogie 采用原生子窗口嵌入，输入事件主要由 CEF 窗口自己处理；本项目 OSR 需要主动把 Qt mouse/key/wheel/IME 事件转成 CEF API。
- Doogie 没有 `CefRenderHandler`、`OnPaint`、`SetAsWindowless`、`OnAcceleratedPaint` 等 OSR 关键实现，OSR 仍以 cefclient 为主要参考。

结论：Doogie 适合作为 Qt + CEF 工程化、handler 分层、profile 持久化、DevTools/下载和部署清单参考；不适合作为离屏渲染核心链路参考。

## OSR 核心机制

CEF windowless rendering 的关键接口是 `CefRenderHandler`。

必须实现：

- `GetViewRect()`：返回浏览器视图矩形，必须是非空矩形。
- `OnPaint()`：接收软件渲染 buffer。CEF header 说明 buffer 是 `width * height * 4` 字节，BGRA 格式，左上角原点；dirty rect 是像素坐标。

建议实现：

- `GetScreenInfo()`：提供屏幕矩形和 `device_scale_factor`，否则 popup 和高 DPI 行为容易出问题。
- `GetRootScreenRect()` / `GetScreenPoint()`：帮助 CEF 理解宿主窗口在屏幕中的位置。
- `OnPopupShow()` / `OnPopupSize()`：处理 select、菜单等 popup。
- `OnAcceleratedPaint()`：二阶段 GPU 路线。Windows 是共享纹理 HANDLE，Linux 是 native buffer plane。CEF 要求回调内重新打开并拷贝到客户端自有纹理，不能缓存句柄。
- `StartDragging()` / `UpdateDragCursor()`：拖拽能力。
- `OnImeCompositionRangeChanged()`：输入法候选窗定位。

浏览器创建要点：

- 使用 `CefWindowInfo::SetAsWindowless(parent_handle)`。
- OSR/windowless 需要设置 Alloy style；cefclient 的 Windows OSR 创建路径中明确断言 windowless rendering requires Alloy style。
- `CefBrowserHost::CreateBrowser()` 异步创建，生命周期通过 `CefLifeSpanHandler::OnAfterCreated` 和 `OnBeforeClose` 管理。

## Qt 承载方案

### 首版：QWidget + QImage + QPainter

优点：

- 实现简单，便于先打通 CEF 生命周期和输入链路。
- `QWidget::paintEvent()` 是 Qt Widgets 的标准绘制入口。
- `QImage` 支持 device pixel ratio，可配合高 DPI。
- CEF `OnPaint` 的 BGRA buffer 在常见 little-endian 平台上可映射为 `QImage::Format_ARGB32`。

代价：

- 每帧需要从 CEF buffer 拷贝到 Qt 持有的图像内存。
- 高频页面或视频页面 CPU 压力较高。

### 二阶段：QOpenGLWidget 纹理上传

优点：

- 可以把 CPU buffer 上传为 OpenGL texture，再由 GPU 合成，减少 Qt raster 绘制压力。
- 适合视频、动画或大分辨率页面。

约束：

- Qt 官方文档要求 OpenGL 资源创建放在 `initializeGL()`，绘制放在 `paintGL()`，不能在构造函数里创建 GL 资源。
- 跨窗口共享 OpenGL 资源可使用 `Qt::AA_ShareOpenGLContexts`。
- 仍然存在 CEF CPU buffer 到 GL texture 的上传成本。

### 三阶段：CEF shared texture

优点：

- 理论上减少 CPU readback/copy。

约束：

- Windows shared texture 是 D3D handle，Qt Widgets 常用 OpenGL 路线，D3D/OpenGL 互操作复杂。
- Qt6 的 RHI/QRhi、ANGLE、D3D11、OpenGL 后端选择会影响方案。
- CEF header 明确 shared texture handle 不可缓存，只能在回调内打开并拷贝到客户端自有纹理。

建议：首版不要把 shared texture 作为必要条件。

## CEF 与 Qt 消息循环集成

可选路线：

| 路线 | 适用 | 说明 |
| --- | --- | --- |
| External message pump | 推荐首选 | CEF 通过 `OnScheduleMessagePumpWork` 通知宿主下一次 work，Qt 用 `QTimer::singleShot` 调 `CefDoMessageLoopWork()`。 |
| Multi-threaded message loop | Windows/Linux 可评估 | CEF 自己跑 UI thread，Qt GUI thread 与 CEF UI thread 分离，所有 Qt 更新必须 queued 到 Qt GUI thread。 |
| CefRunMessageLoop | 不推荐用于 Qt 主窗口 | 会阻塞，除非应用完全由 CEF 驱动。 |

首版建议采用 external message pump，因为 Qt 主事件循环仍是 `QApplication::exec()`，CEF work 由 Qt timer 驱动，线程关系更容易推理。

## 输入与 IME 调研结论

Qt 需要把以下事件转给 CEF browser host：

- resize：`WasResized()`
- focus：`SetFocus(true/false)`
- mouse move/press/release/double click：`SendMouseMoveEvent()`、`SendMouseClickEvent()`
- wheel：`SendMouseWheelEvent()`
- keyboard：`SendKeyEvent()`
- IME：`ImeSetComposition()`、`ImeCommitText()`、`ImeCancelComposition()`

注意点：

- Qt mouse position 是逻辑坐标，CEF view 坐标也按 DIP 处理，buffer 尺寸才是物理像素。
- Keyboard native code 在 Windows/Linux 上差异较大，建议直接参考 cefclient 平台实现。
- 中文输入要处理 preedit、commit、候选窗位置；只转 key event 不足以完成输入法体验。

## 构建与部署结论

CEF binary distribution 典型内容包括：

- `include/`
- `libcef_dll/`
- `Debug/`、`Release/`
- `Resources/`
- `tests/cefclient`、`tests/cefsimple`、`tests/ceftests`

项目需要链接：

- CEF shared library：Windows 为 `libcef.dll` 与 import lib；Linux 为 `libcef.so`。
- `libcef_dll_wrapper` 静态库。
- Qt Widgets，后续如使用 OpenGL 则增加 Qt OpenGL/OpenGLWidgets。

平台构建与部署优先级：

- V1 只建立 Windows x64（`windows64`/`win64`）+ Qt 5.14.2 MSVC2017 64bit 构建、运行和部署闭环。
- V2 再根据情况评估 Qt 6.9.3 支持，重点验证 CMake、Widgets API、OpenGL/RHI 和部署差异。
- V2 如补 `win32` 构建矩阵，重点验证 32-bit CEF/Qt/编译器架构一致性。
- V2 如补 `linux-amd64` 构建矩阵，重点验证运行时库搜索路径、sandbox 策略和系统依赖。

部署需要包含：

- CEF 主库。
- `icudtl.dat`。
- `chrome_*.pak`、`resources.pak`、locales。
- V8 snapshot 文件。
- 平台相关 GPU/EGL/GLES 库。
- 子进程可执行文件。

## 主要风险

| 风险 | 影响 | 建议 |
| --- | --- | --- |
| Qt event loop 与 CEF message loop 集成不稳 | 卡顿、退出崩溃、回调线程错乱 | 首版固定 external message pump，并统一封装线程投递。 |
| 高 DPI 坐标混乱 | 点击位置偏移、画面模糊、popup 位置错误 | 明确 DIP/物理像素转换，统一 `devicePixelRatioF()` 与 `device_scale_factor`。 |
| 中文 IME 缺失 | 中文输入不可用 | 首版纳入验收，参考 cefclient OSR IME。 |
| 弹窗未合成 | select/menu 不显示或位置错误 | 实现 `OnPopupShow/OnPopupSize` 和 `PET_POPUP` buffer 合成。 |
| CEF 关闭顺序错误 | 退出时崩溃或进程残留 | 按 `TryCloseBrowser`、`OnBeforeClose`、`CefShutdown` 管理生命周期。 |
| 过早上 shared texture | 拉长首版周期 | 先 CPU buffer，性能数据明确后再做 GPU 路线。 |
