# 可视化设计系统兼容性要求

目标：无论底层选择 CEF、Electron、Qt WebEngine、Edge WebView2 还是其他 Chromium 内核方案，浏览器都必须能完整加载目标可视化设计系统，而不是只做到页面不崩溃。

## 内核版本

| 等级 | Chromium 版本 | 结论 |
| --- | --- | --- |
| 最低 | Chromium 90+ | 基本满足 ES6、Flex、CSS Transform、Canvas、LocalStorage、HTML5 Video 等能力。 |
| 推荐 | Chromium 100+ | 更适合作为长期维护的最低线。 |
| 最佳 | Chromium 120+ | 推荐作为新项目目标线，兼容性和 Web API 覆盖更稳。 |

当前 CEF 文档调研时 stable channel 为 Chromium/CEF 150，已经高于最佳目标线。Electron、Qt WebEngine、Edge WebView2 方案也必须确认实际打包或运行时对应的 Chromium 版本，而不是只看开发机浏览器版本。

## 最低运行能力

| 能力 | 是否必须 | 验收方式 | 失败表现 |
| --- | --- | --- | --- |
| JavaScript | 必须 | 页面脚本正常执行，控制台无阻塞级错误 | 页面空白或只有 noscript 提示。 |
| ES6 | 必须 | module/class/let/const/promise/async 等语法正常 | 脚本解析失败。 |
| Flex | 必须 | 布局与 Chrome 基准一致 | 面板错位、控件重叠。 |
| CSS Transform/Animation | 必须 | 缩放、旋转、过渡、动画正常 | 动画缺失或掉帧明显。 |
| HTML5 Video | 必须 | MP4/H.264/AAC 可播放 | 视频黑屏、解码失败。 |
| Canvas | 必须 | 二维码、画布元素可绘制和导出 | 二维码空白或绘制异常。 |
| LocalStorage | 必须 | 语言、用户偏好等本地状态可读写并持久化 | 国际化、偏好设置或缓存状态失效。 |
| XHR / Fetch | 必须 | API 请求能发出并返回正确结果 | 数据区空白、接口失败。 |
| Cookie | 登录场景必须 | 登录态跨请求携带 | API 返回 401。 |
| WebSocket | 很可能必须 | 实时数据、协同或状态推送可连接 | 实时刷新失败。 |
| GPU 加速 | 强烈建议 | 动画、视频、Canvas 性能稳定 | CPU 占用高、动画掉帧、视频卡顿。 |

## JavaScript

目标页面完全依赖 JavaScript，浏览器必须默认开启 JS。不要为了安全策略全局禁用 JS。

实现要求：

- CEF、Electron、Qt WebEngine、WebView2 均保持 JavaScript 默认开启。
- 如果提供安全模式，必须明确区分普通浏览模式与禁 JS 模式。
- 加载失败时要能通过 DevTools console 定位 JS error。

验收：

- 页面入口脚本执行。
- 主要组件完成 mount/render。
- 禁用 JS 的测试只用于确认错误提示，不作为正常运行模式。

## LocalStorage

目标页面会通过 LocalStorage 保存国际化、偏好设置或运行状态，因此必须支持并持久化。

实现要求：

- CEF 必须设置稳定的 `cache_path` 或持久化 `CefRequestContext`，避免每次启动都丢 LocalStorage。
- Electron/WebView2/Qt WebEngine 必须使用持久 profile，不使用临时 profile 承载正式业务。
- 隐私/无痕模式不能作为默认运行模式。

验收：

- 写入 `localStorage` 后刷新页面仍可读取。
- 关闭浏览器重启后仍可读取。
- 国际化状态不丢失。

## HTML5 Video 与 Codec

Chromium 内核支持 HTML5 Video 标签，但能否播放某种视频取决于 codec。

建议基线：

- 必须支持 MP4/H.264/AAC。
- WebM/VP8/VP9 可作为补充。
- HEVC/H.265 不作为默认可用能力，必须按内核发行包、操作系统和编译配置单独验证。

不同方案注意点：

| 方案 | 注意点 |
| --- | --- |
| CEF | 默认发行包通常不应假设 HEVC 可用；如业务需要专有 codec，要用测试页验证具体包。 |
| Electron | 以实际 Electron 版本和打包的 ffmpeg 能力为准。 |
| Qt WebEngine | 如果视频失败，优先检查 proprietary codecs、平台 codec 与 Qt WebEngine 构建配置。 |
| Edge WebView2 | 以安装的 Edge WebView2 Runtime 和系统 codec 能力为准。 |

验收：

- H.264/AAC MP4 自动或手动播放正常。
- 控制台和 Network 无 codec/media error。
- 业务视频格式清单与内核实际支持列表一致。

## Canvas

二维码和部分图形渲染依赖 Canvas。

实现要求：

- 2D Canvas 必须可用。
- 如图表或 3D 组件依赖 WebGL，需要启用 WebGL 并验证 GPU 黑名单情况。
- 离屏渲染模式下 Canvas 输出必须能进入 `OnPaint` 或 GPU texture 链路。

验收：

- 二维码显示正常。
- Canvas 动画刷新正常。
- `toDataURL`、截图或导出能力按业务需要验证。

## CSS3 与 GPU 加速

页面大量依赖 CSS Transform、Transition、Animation 和复杂布局。CPU 软件渲染可以打开页面，但大型设计器和播放器会明显变慢。

建议：

- 保持 GPU acceleration 默认开启。
- 不默认添加 `--disable-gpu`。
- 对 CEF OSR 首版可以先用 CPU buffer，但要保留 GPU/accelerated paint 的演进空间。

验收：

- 动画无明显掉帧。
- 视频播放不卡顿。
- 复杂大屏缩放、拖拽、编辑时 UI 响应稳定。

## 网络能力

### XHR / Fetch

必须允许页面访问业务 API。

验收：

- API 请求正常发出。
- 请求头、Cookie、Authorization 按业务要求携带。
- 失败时能在 DevTools Network 中定位。

### Cookie

如果后台需要登录，浏览器必须允许 Cookie，且使用持久 profile。

要求：

- 不默认阻断 first-party cookie。
- 如有 third-party cookie 场景，需按目标域名拓扑单独验证。
- CEF RequestContext、Qt WebEngine profile、WebView2 profile 都要保持稳定。

### WebSocket

可视化平台很可能使用 WebSocket 做实时数据、协同编辑、预览刷新或运行状态推送。

验收：

- `ws://` 或 `wss://` 连接建立。
- 心跳不断线。
- 断线重连行为符合页面预期。

### Mixed Content

如果页面是 HTTPS，但 API、视频或 WebSocket 仍使用 HTTP/WS，Chromium 会按 Mixed Content 策略拦截部分请求。

建议：

- 正式环境统一 HTTPS/WSS。
- 不把放开 Mixed Content 作为默认生产策略。
- 开发环境如需放开，必须通过显式启动参数或白名单，并写入风险说明。

### CORS

CORS 主要应由服务端正确配置，不建议在浏览器端全局绕过。

验收：

- API 域名返回正确 `Access-Control-Allow-Origin`。
- 需要 Cookie 的跨域请求正确配置 credentials。
- 预检 OPTIONS 请求返回正确状态码和 header。

## Chromium 方案配置建议

### CEF

建议默认保持以下能力开启：

- JavaScript
- LocalStorage / DOM Storage
- WebGL
- WebSocket
- Cookie
- XHR / Fetch
- GPU acceleration
- DevTools 或 remote debugging，至少开发期启用

CEF 重点配置：

- 选择 release branch，不直接跟 master。
- 配置持久 `cache_path`。
- 配置稳定 `user_data_path`。
- 不默认使用 incognito request context。
- 不默认添加 `--disable-javascript`、`--disable-local-storage`、`--disable-gpu`、`--disable-webgl`。
- 开发期可配置 `remote_debugging_port`。

### Electron

重点确认：

- Electron 实际 Chromium 版本。
- `webPreferences.javascript` 不禁用。
- 持久 session 使用正确 partition。
- 需要 Node 能力时要区分页面安全边界，不把 Node 集成暴露给不可信业务页。
- codec 能力用真实视频测试，不凭版本假设。

### Qt WebEngine

重点确认：

- Qt WebEngine 对应 Chromium 版本。
- `JavascriptEnabled`、`LocalStorageEnabled` 等 settings 保持开启。
- 使用持久 `QWebEngineProfile`。
- 如视频失败，检查 proprietary codecs 和平台 codec。
- 如果设计器依赖拖拽、剪贴板、文件选择，补齐 Qt 层事件与权限处理。

### Edge WebView2

重点确认：

- 目标机器安装的 WebView2 Runtime 版本。
- 使用固定 user data folder 保持 LocalStorage/Cookie。
- JS、WebMessage、DevTools、权限弹窗策略符合业务需要。
- 通过 WebView2 DevTools 或远程调试定位页面问题。

## 设计器模式额外能力

如果加载的是 Designer，而不是只读 Renderer，需要额外验收：

| 能力 | 是否需要 | 说明 |
| --- | --- | --- |
| Drag & Drop | 必须 | 组件拖拽、素材拖入画布。 |
| Clipboard API | 必须 | 复制、粘贴、跨组件编辑。 |
| Clipboard 事件 | 必须 | `copy`、`cut`、`paste`。 |
| ResizeObserver | 必须 | 面板、画布和组件尺寸响应。 |
| MutationObserver | 必须 | DOM 变化监听。 |
| File API | 必须 | 导入图片、视频、配置文件。 |
| PointerEvent | 必须 | 统一鼠标、触控、压感笔输入。 |
| MouseEvent | 必须 | 基础点击、拖拽、悬停。 |
| KeyboardEvent | 必须 | 快捷键、文本编辑。 |
| Fullscreen API | 必须 | 预览或播放全屏。 |
| WebGL | 可能 | 3D 图表、地图、复杂可视化。 |
| IndexedDB | 可能 | 本地缓存、离线数据、设计草稿。 |
| Web Worker | 可能 | 大数据处理、图形计算、导入导出。 |
| WebSocket | 必须 | 实时预览、协同、数据推送。 |

## 验收清单

- Chromium 内核版本满足 90+，推荐达到 100+，最佳达到 120+。
- JavaScript 默认开启。
- LocalStorage 可用且跨重启持久化。
- Cookie 可用，登录接口不返回非预期 401。
- XHR/Fetch 正常访问业务 API。
- WebSocket 能连接、收发、断线重连。
- Canvas 二维码和图形绘制正常。
- HTML5 Video 播放业务视频格式。
- CSS Transform、Flex、动画表现与 Chrome 基准一致。
- GPU acceleration 默认开启，性能达到业务可用水平。
- HTTPS 页面无非预期 Mixed Content 拦截。
- CORS 由服务端正确配置，无浏览器端全局绕过。
- Designer 模式下 Drag & Drop、Clipboard、File API、PointerEvent、KeyboardEvent、Fullscreen API 可用。
