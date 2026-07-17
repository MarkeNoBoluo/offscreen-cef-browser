# 文档总览

本文档集面向”用 C++ + CEF + Qt Widgets 开发离屏渲染浏览器”的设计与实现。V1 主要支持 Qt 5.14.2 MSVC2017 64bit。调研日期为 2026-07-16，代码实现已完成 V2.5.1（多 Tab 页浏览）。

## 阅读顺序

1. [research.md](research.md)：先看调研结论，确认 CEF OSR、cefclient 和 Qt 的关键约束。
2. [visualization-system-compatibility.md](visualization-system-compatibility.md)：确认目标可视化设计系统需要的浏览器能力。
3. [architecture.md](architecture.md)：再看目标架构，重点是进程模型、消息循环、渲染链路和输入链路。
4. [implementation-plan.md](implementation-plan.md)：最后看阶段计划、验收标准和风险清单。

## 结论摘要

- CEF 是多进程架构。主应用一般是 browser process，Blink/V8 在 renderer process，GPU 与其他服务进程由 Chromium 按需启动。
- 离屏渲染应实现 `CefRenderHandler`，核心回调是 `GetViewRect`、`GetScreenInfo`、`OnPaint`，弹窗和输入法还需要覆盖对应回调。
- cefclient 已经包含 OSR 路线，重点参考 `client_handler_osr.*`、`browser_window_osr_*`、`osr_render_handler_*`、`osr_renderer.*`。
- Qt 侧首版建议使用 QWidget + QImage/QPainter，先降低 CEF 集成风险；性能瓶颈明确后再引入 QOpenGLWidget。
- 高 DPI、输入法、弹窗、焦点和关闭生命周期不是附加项，必须进入首个可用版本的验收。
- 目标可视化设计系统最低按 Chromium 90+，推荐 Chromium 100+，最佳 Chromium 120+；同时必须启用 JavaScript、LocalStorage、Canvas、HTML5 Video、Cookie、XHR/Fetch、WebSocket 和常见设计器输入能力。
- 版本路线分两版推进：V1 仅实现 Windows x64（`windows64`/`win64`）+ Qt 5.14.2 MSVC2017 64bit；V2 再根据情况评估 Qt 6.9.3、`win32` 和 `linux-amd64`；不考虑 macOS。

## 术语

| 术语 | 含义 |
| --- | --- |
| OSR | Off-Screen Rendering，CEF 中通常对应 windowless rendering。 |
| DIP | Device Independent Pixel，CEF/Qt 高 DPI 坐标体系中的逻辑像素。 |
| View buffer | `OnPaint` 收到的浏览器画面 buffer，CEF 文档定义为 BGRA、左上角原点。 |
| Popup buffer | 下拉框、select、菜单等弹出元素在 OSR 下可能通过 `PET_POPUP` 单独绘制。 |
| Alloy style | CEF 传统嵌入式浏览器风格；cefclient 的 OSR 路径要求 windowless browser 使用 Alloy style。 |
| External message pump | 由宿主程序驱动 CEF 消息循环，适合与 Qt event loop 集成。 |

## 当前范围

已完成：

- CEF/cefclient 前期调研。
- 可视化设计系统兼容性要求。
- CEF + Qt OSR 架构设计。
- 分阶段实现路线与源码实现（V1 ~ V2.5.1）。
- 首版验收与风险清单。
- V1/V2 Qt 版本与平台支持路线。

未覆盖：

- CEF 二进制包下载与提交策略。
- CI/CD 与安装包脚本。
