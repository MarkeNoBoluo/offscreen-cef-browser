# Qt OpenGL 与 CEF OSR GPU 链路设计

## 目标

在 Windows x64、CEF 100、Qt 6.9.3 和 VS2022 环境中，将 CEF OSR 的 D3D11 共享纹理直接交给 Qt OpenGL 组件显示。正常 GPU 路径不得执行 D3D staging texture、`Map`、`QImage` 或 CPU 像素回读。

首个实现以当前测试机跑通为目标，使用 `QOpenGLWidget` 和 `WGL_NV_DX_interop`。当运行环境不支持互操作或初始化失败时，保留现有 CPU 回读路径，避免黑屏。

## 不在本次范围内

- 不承诺当前实现覆盖所有 Intel、AMD 和 NVIDIA 驱动组合。
- 不改动 CEF/Qt 生命周期、输入、IME、拖拽、导航和多标签页接口。
- 不引入 Qt Quick、QRhi 私有 API或纯 D3D11 原生显示控件。
- 不把“CPU 像素上传 OpenGL”计为完整 GPU 链路成功。

## 现有链路

当前 `OnPaint` 路径为：

`CEF BGRA buffer -> BrowserFrame::SetViewImage -> QImage -> BrowserWidget::paintEvent -> QPainter::drawImage`

当前 `OnAcceleratedPaint` 已能打开 D3D11 共享纹理，但随后执行：

`CEF shared texture -> CopyResource(staging) -> Map -> CPU buffer -> QImage -> QPainter`

因此即使 accelerated callback 开始触发，现有实现仍包含 GPU 到 CPU 的同步回读。

## 方案比较

### 方案一：QOpenGLWidget 与 D3D11 互操作

CEF 临时共享纹理在回调内复制到应用自有 D3D11 纹理。Qt OpenGL 上下文通过 `WGL_NV_DX_interop` 注册应用纹理，`paintGL()` 锁定互操作对象后直接采样。

优点：正常帧只发生 GPU 内部复制和 GL 合成，没有 CPU 像素回读；符合用户指定的 GL 组件。缺点：依赖当前显卡驱动提供 WGL/D3D 互操作扩展。

这是本次采用的方案。

### 方案二：CPU 图像上传 OpenGL

保留 `OnPaint` 或 D3D11 `Map`，再用 `glTexSubImage2D` 上传。

优点：兼容性好、实现简单。缺点：仍有完整帧 CPU 拷贝或 GPU 到 CPU 再到 GPU 的往返，不能作为 GPU 链路验收结果，仅作为回退路径。

### 方案三：Qt 控件直接使用 D3D11

在原生窗口中用 D3D11 swap chain 显示应用纹理。

优点：与 CEF Windows 共享纹理后端一致，兼容范围通常优于 WGL 互操作。缺点：不使用 GL 组件，不符合本次要求。

## 组件设计

### GpuFrameBridge

新增共享桥接对象，由 `BrowserWidget` 和 `OsrRenderHandler` 共同持有。

职责：

- 在 `OnAcceleratedPaint` 回调有效期内调用 `ID3D11Device::OpenSharedResource`。
- 为主视图和 popup 分别维护应用自有 `ID3D11Texture2D`。
- 在尺寸或格式变化时重建应用纹理，并递增资源代次。
- 使用 `CopyResource` 将 CEF 纹理复制到应用纹理，然后立即释放 CEF 纹理引用。
- 保存尺寸、格式、帧序号、资源代次和最后错误，但不保存 CEF 传入的共享句柄。
- 使用互斥量保护纹理元数据及 D3D/GL 访问窗口。

应用纹理必须能被 WGL 互操作注册，并保持到下一次尺寸变化或桥接对象销毁。主视图和 popup 使用独立槽位，避免 popup 覆盖主视图资源。

### OsrRenderHandler

`OnAcceleratedPaint` 的正常流程改为：

1. 记录 accelerated paint 统计。
2. 调用 `GpuFrameBridge::CopyFromSharedHandle`。
3. 成功时只请求 Qt 更新，不调用 `ReadSharedTexture` 和 `BrowserFrame::SetViewImage`。
4. 失败时调用现有 CPU 回读逻辑，并记录回退原因。

`OnPaint` 保留，用于 CEF 未提供 accelerated callback 的情况。它继续更新 `BrowserFrame`，由 Qt OpenGL 控件上传 CPU 图像显示。

### BrowserWidget

`BrowserWidget` 从 `QWidget` 改为 `QOpenGLWidget`，输入、IME、拖拽、焦点和尺寸处理接口保持不变。

- `initializeGL()`：加载 WGL 互操作函数，创建 GL texture、shader 和全屏四边形资源。
- `paintGL()`：优先绘制 `GpuFrameBridge` 中的主视图纹理，再按 popup rect 叠加 popup 纹理。
- 当桥接资源代次变化时，仅在当前 GL context 下注销旧互操作对象并注册新 D3D11 纹理。
- 每次绘制前调用 WGL lock，绘制结束后调用 WGL unlock。
- GPU 互操作不可用或当前帧来自 `OnPaint` 时，将最新 `BrowserFrame` 上传为 GL texture 后显示。
- `resizeGL()` 不改变 CEF 的 DIP 语义；现有 `resizeEvent()` 继续调用 `WasResized` 链路。

OpenGL 资源只能在 GL context 当前时创建、注册、注销和销毁。析构阶段调用 `makeCurrent()` 清理互操作与 GL 资源，再调用 `doneCurrent()`。

## 数据流

正常 GPU 路径：

`CEF GPU process -> CEF D3D11 shared texture -> OpenSharedResource -> CopyResource(app texture) -> WGL interop -> QOpenGLWidget::paintGL`

回退路径：

`CEF OnPaint/accelerated texture -> CPU BGRA/QImage -> GL texture upload -> QOpenGLWidget::paintGL`

回退路径保证可见性，但不会计入 GPU 链路成功帧。

## 同步与帧调度

- CEF 临时共享句柄只在回调内打开和复制。
- 桥接对象保留最新帧，不为每一帧排队，延续现有 latest-frame/backpressure 策略。
- Qt 更新仍通过 queued `update()` 合并，避免跨线程直接调用 GL。
- D3D 写入与 GL 采样由桥接互斥量和 WGL lock/unlock 串行化。
- 尺寸变化通过资源代次触发重新注册，旧资源只有在 GL 侧注销后才释放。

## 错误处理与回退

以下情况进入 CPU 回退：

- 缺少任一必需的 `WGL_NV_DX_interop` 函数。
- 无法用匹配适配器打开 CEF 共享纹理。
- 无法创建或注册应用 D3D11 纹理。
- WGL lock、unlock 或重新注册失败。
- CEF 继续只提供 `OnPaint`。

错误只在状态变化或采样频率内记录，避免热路径刷屏。GPU 路径恢复前保持回退状态；资源尺寸变化时允许重新尝试初始化。

## 遥测

日志和周期统计至少增加：

- `gpu_present_path=wgl_dx_interop|cpu_gl_upload|qimage_fallback`
- `d3d_to_gl_frames`
- `cpu_readback_fallback_frames`
- WGL 互操作支持状态和失败阶段
- D3D11 适配器名称、vendor/device ID
- 应用纹理尺寸、格式和资源代次

`OnAcceleratedPaint` 只证明 CEF 提供了共享纹理；只有 `d3d_to_gl_frames` 增长且回读计数不增长，才证明 Qt 端 GPU 链路实际工作。

## 测试与验收

### 自动化测试

- 为桥接状态和呈现路径选择建立不依赖真实 GPU 的状态机测试。
- 验证 accelerated copy 成功时不选择 CPU 回读。
- 验证互操作缺失、打开共享纹理失败和 `OnPaint` 输入时选择 CPU 回退。
- 验证主视图与 popup 使用独立资源代次。
- 运行现有全部单元测试，避免输入、几何、日志和统计回归。

真实 D3D11/WGL 互操作依赖驱动和窗口上下文，不用 mock 结果冒充运行验收。

### 构建验收

- CMake 引入 Qt `OpenGL` 和 `OpenGLWidgets` 组件。
- VS2022 x64 Release 构建成功并通过 `windeployqt` 部署相应 Qt DLL。
- CTest 全部通过。

### 运行验收

在当前测试机运行高频动画或视频页面，必须同时满足：

- `OnAcceleratedPaint > 0`。
- `d3d_to_gl_frames > 0` 且持续增长。
- 正常 GPU 帧不调用 staging texture、`Map` 或 `BrowserFrame::SetViewImage`。
- `gpu_present_path=wgl_dx_interop`。
- 主画面、缩放、窗口 resize 和 popup 显示正常。
- 关闭流程完成，无 GPU/renderer 残留进程。

关闭 GPU 或模拟互操作初始化失败后，还应确认 CPU 回退画面可见，并出现明确的回退原因和计数。
