# 实施计划

本文给出从空仓库到首个可用离屏渲染浏览器的分阶段计划。每个阶段都应有可验证产物，不把风险堆到最后。

## 阶段 0：文档与基线

状态：已完成本文档集。

产物：

- 根目录 `README.md`
- `docs/research.md`
- `docs/visualization-system-compatibility.md`
- `docs/architecture.md`
- `docs/implementation-plan.md`

验收：

- 明确 CEF release branch 选择策略。
- 明确首版采用 CPU OSR buffer + QWidget/QPainter。
- 明确 Qt/CEF 消息循环集成方式。
- 明确目标可视化设计系统的浏览器能力矩阵。
- 明确版本路线：V1 Windows x64（`windows64`/`win64`）+ Qt 5.14.2 MSVC2017 64bit；V2 再根据情况评估 Qt 6.9.3、`win32` 和 `linux-amd64`；不考虑 macOS。

## 阶段 1：工程骨架

目标：建立可编译、可启动、可退出的 Qt + CEF 空壳。

任务：

- 创建 CMake 顶层工程。
- 增加 Qt 5.14.2 MSVC2017 64bit 查找与链接逻辑。
- 增加 CEF binary distribution 路径配置，例如 `CEF_ROOT`。
- V1 只配置 Windows x64（`windows64`/`win64`）+ Qt 5.14.2 MSVC2017 64bit 构建，确保 CEF、Qt、MSVC2017 工具链和 CMake 生成器全部为 x64。
- 编译 `libcef_dll_wrapper` 或接入 CEF binary distribution 自带 CMake 配置。
- 创建主程序 target 和 subprocess target。
- 实现 `CefExecuteProcess`、`CefInitialize`、Qt event loop、`CefShutdown`。
- 输出 CEF log 文件到可配置目录。

验收：

- 程序启动后显示一个 Qt 主窗口。
- 退出后无 CEF 子进程残留。
- Debug/Release 都能找到 CEF 运行时资源。
- Windows x64（`windows64`/`win64`）+ Qt 5.14.2 MSVC2017 64bit Debug/Release 均能构建并启动。

## 阶段 2：创建 OSR Browser

目标：创建 windowless browser 并加载页面。

任务：

- 实现 `BrowserApp`、`BrowserService`、`BrowserClient`。
- 实现最小 `CefLifeSpanHandler`。
- 构造 `CefWindowInfo::SetAsWindowless(parent_handle)`。
- 设置 Alloy style。
- 实现 `GetViewRect` 和 `GetScreenInfo`。
- 通过工具栏或默认 URL 加载页面。

验收：

- `OnAfterCreated` 能拿到有效 `CefBrowser`。
- 能加载 `https://example.com` 或本地 HTML。
- resize 后 CEF 收到新 view rect。

## 阶段 3：软件渲染显示

目标：把 `OnPaint` 的 buffer 显示到 Qt。

任务：

- 实现 `OsrRenderHandler::OnPaint`。
- 拷贝 BGRA buffer 到 `BrowserFrame`。
- Qt widget 用 `QImage::Format_ARGB32` 绘制。
- 支持 dirty rect 到 Qt update region 的转换。
- 处理 popup buffer：`OnPopupShow`、`OnPopupSize`、`PET_POPUP`。
- 处理高 DPI：`devicePixelRatioF()`、`QImage::setDevicePixelRatio()`、`CefScreenInfo.device_scale_factor`。

验收：

- 页面可见，滚动和动画能更新。
- 浏览器窗口 resize 后画面无拉伸错位。
- 高 DPI 显示器下点击和画面位置一致。
- select/dropdown popup 能显示在正确位置。

## 阶段 4：输入、焦点、IME

目标：浏览器可交互，中文输入可用。

任务：

- 鼠标 move/press/release/double click 转 CEF mouse event。
- wheel 转 CEF wheel event。
- focus in/out 转 `SetFocus`。
- key press/release 转 `CefKeyEvent`。
- `QInputMethodEvent` 转 CEF IME composition/commit/cancel。
- 实现输入法候选窗位置更新。
- 设置 Qt widget 属性：focus policy、input method enabled、mouse tracking。

验收：

- 链接点击、文本框输入、快捷键、滚轮滚动可用。
- 中文拼音输入、候选、提交可用。
- 输入框光标位置与 IME 候选窗位置合理。

## 阶段 5：浏览器基础功能

目标：达到最小可用浏览器。

任务：

- 地址栏：输入 URL、回车导航。
- 前进、后退、刷新、停止。
- 标题和加载进度。
- 右键菜单基础项。
- 下载回调和保存路径。
- DevTools 打开按钮。
- renderer crash 提示和 reload。

验收：

- 常规网页可以浏览。
- DevTools 可打开并 inspect 当前页面。
- 下载一个小文件能成功保存。
- renderer crash 后 UI 不崩溃。

## 阶段 6：可视化设计系统兼容性验收

目标：确认目标页面不是“能打开首页”，而是播放器和设计器核心链路都可用。

任务：

- 准备兼容性测试页，覆盖 ES6、Flex、CSS Transform、Canvas、LocalStorage、Video、Fetch、Cookie、WebSocket。
- 准备目标设计系统真实页面测试用例。
- 验证 JavaScript 默认开启，禁用 JS 时有明确错误提示。
- 验证 LocalStorage 在重启后保持数据。
- 验证 Cookie 与登录态，确认接口不因 Cookie 策略返回 401。
- 验证 H.264/AAC MP4 视频；如业务存在 HEVC/H.265，单独记录内核与编译包支持情况。
- 验证 XHR/Fetch、CORS、Mixed Content 和证书错误策略。
- 验证设计器能力：Drag & Drop、Clipboard、File API、PointerEvent、KeyboardEvent、Fullscreen API。
- 验证 WebGL、IndexedDB、Web Worker 是否按目标页面实际依赖启用。

验收：

- 目标可视化播放器页面完整加载，Canvas 二维码、视频、动画、接口请求正常。
- 目标设计器页面可拖拽、复制粘贴、上传文件、全屏预览。
- DevTools console 无阻塞级 JS error。
- Network 面板无非预期 401、CORS、Mixed Content、codec 失败。

## 阶段 7：封装与部署

目标：生成 V1 Windows x64（`windows64`/`win64`）可交付目录。

任务：

- CMake install 规则。
- 拷贝 CEF runtime、resources、locales、snapshot、subprocess。
- 拷贝 Qt platform plugins 和必要 DLL。
- Windows x64（`windows64`/`win64`）处理 `libEGL.dll`、`libGLESv2.dll`、`vk_swiftshader` 等 CEF 相关库。

验收：

- 在干净目录运行可执行文件。
- 资源路径不依赖开发机绝对路径。
- 关闭后无进程残留。
- Windows x64（`windows64`/`win64`）可交付目录能独立启动并加载页面。

## 阶段 8：V2 版本与平台扩展评估

目标：在 V1 功能稳定后，根据实际项目需要评估 Qt 6.9.3、`win32` 和 `linux-amd64` 的支持范围。

任务：

- 评估 Qt 6.9.3 与 V1 Qt 5.14.2 代码、CMake、部署结构的差异。
- 如决定支持 `win32`，增加 `win32` 构建配置，确保 CEF、Qt、编译器和 CMake 生成器全部为 x86。
- 如决定支持 `linux-amd64`，增加 `linux-amd64` 构建配置，处理 `libcef.so`、rpath、Qt platform plugin、系统库依赖和 `chrome-sandbox` 权限策略。
- 抽离平台相关部署逻辑，避免 Windows DLL 列表和 Linux so/rpath 规则互相污染。
- 对最终纳入 V2 的 Qt 版本和平台分别运行 V1 核心功能验收。

验收：

- Qt 6.9.3 是否进入 V2 支持范围有明确结论和风险记录。
- 纳入 V2 的平台能构建、启动 Qt 主窗口、加载页面、显示 OSR 画面并正常退出。
- 纳入 V2 的运行目录不依赖开发机绝对路径。
- 纳入 V2 的平台关闭后无 CEF 子进程残留。

## 阶段 9：性能优化

目标：在真实页面负载下优化渲染。

任务：

- 记录帧率、paint 次数、buffer 拷贝耗时。
- 减少全量 repaint，优先 dirty rect。
- 评估 QOpenGLWidget 纹理上传。
- 评估 external begin frame。
- 评估 shared texture，但只在性能数据证明必要时执行。

验收：

- 大页面滚动不卡顿。
- 视频/动画页面 CPU 占用有量化数据。
- 优化前后指标可对比。

## CMake 方向

V2 可评估的 Qt6.9.3 方向：

```cmake
find_package(Qt6 REQUIRED COMPONENTS Widgets)
target_link_libraries(app PRIVATE Qt6::Widgets)
```

V1 固定 Qt 5.14.2 MSVC2017 64bit：

```cmake
find_package(Qt5 REQUIRED COMPONENTS Widgets)
target_link_libraries(app PRIVATE Qt5::Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
```

如 V2 决定同时支持 Qt5 与 Qt6，再封装版本选择：

```cmake
find_package(Qt6 QUIET COMPONENTS Widgets OpenGLWidgets)
if(Qt6_FOUND)
  set(QT_PACKAGE Qt6)
else()
  find_package(Qt5 REQUIRED COMPONENTS Widgets)
  set(QT_PACKAGE Qt5)
endif()
```

## 首版验收清单

- 启动：能显示 Qt 主窗口。
- 初始化：CEF log 无 fatal error。
- 加载：能打开默认 URL。
- 渲染：首帧显示，后续 repaint 正常。
- 输入：鼠标、滚轮、键盘、中文 IME 可用。
- DPI：100%、150%、200% 缩放下坐标正确。
- Popup：HTML select 能显示和点击。
- 可视化系统：JS、LocalStorage、Video、Canvas、Fetch、Cookie、WebSocket、Clipboard、Drag & Drop、File API 可用。
- DevTools：能打开并 inspect。
- 退出：关闭窗口后进程完全退出。
- 平台与版本：V1 仅要求 Windows x64（`windows64`/`win64`）+ Qt 5.14.2 MSVC2017 64bit 通过上述验收；Qt 6.9.3、`win32` 和 `linux-amd64` 放入 V2 评估。

## 风险处理顺序

1. 先解决生命周期和消息循环。
2. 再解决渲染显示。
3. 再解决输入、DPI、IME。
4. 最后做性能和 shared texture。

原因：生命周期错误会导致无法稳定调试；DPI/IME 是用户可见基础能力；shared texture 属于优化，不应阻塞首个可用版本。
