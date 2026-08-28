# 三种场景应用

当前版本提供三个直接可用的应用场景：

| 场景 | 入口 | 适用范围 |
| --- | --- | --- |
| 最小浏览器外壳 | `offscreen_cef_browser.exe` / `src/app/main.cpp` | 需要一个可独立运行、可继续改造的 Qt 浏览器程序。 |
| WebView Browser 快速嵌入 | `embedding_demo_webview.exe` / `embedding_demo/webview/main.cpp` | 宿主程序只需要嵌入一个页面、全屏页或指令驱动页面。 |
| Tabbed Browser 快速嵌入 | `embedding_demo_tabbed_browser.exe` / `embedding_demo/tabbed_browser/main.cpp` | 宿主程序需要内置多标签页浏览能力。 |

## 共同构建入口

三个场景由同一套构建脚本生成：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\scripts\build.ps1" `
  -Architecture x64 `
  -CefRoot "D:\Git\cef_binary_100.0.14+g4e5ba66+chromium-100.0.4896.75_windows64_minimal" `
  -QtPrefix "D:\IDE\QT6.9.3\6.9.3\msvc2022_64" `
  -BuildDir "build\cef100-msvc2022-x64" `
  -Configuration Release
```

生成目标位于 `bin\msvc-2022-x64\Release`：

- `offscreen_cef_browser.exe`
- `offscreen_cef_subprocess.exe`
- `embedding_demo_webview.exe`
- `embedding_demo_tabbed_browser.exe`

## 场景一：最小浏览器外壳

`offscreen_cef_browser.exe` 是可独立运行的最小浏览器外壳。它已经包含进程级
CEF 初始化、多 Tab 管理、地址栏、新建 Tab、标题同步、异步关闭和 IME 原生消息
路由。

运行方式：

```powershell
& ".\bin\msvc-2022-x64\Release\offscreen_cef_browser.exe" `
  --url=https://example.com/
```

改造步骤：

1. 在 `src/app/main.cpp` 修改 `TabbedBrowserWindow`，调整地址栏、Tab 控件、
   新建按钮和窗口样式。
2. 在 `src/app/app_config.h` 修改默认首页，或继续通过 `--url=` 从命令行覆盖
   首个标签页地址。
3. 通过 `TabManager::CreateTab()`、`RequestCloseTab()`、`ShutdownAll()` 管理
   标签生命周期。
4. 通过 `TabManager::ActiveBrowserService()` 取得当前页服务，再调用
   `Navigate()`、`Reload()`、`Stop()` 等浏览器动作。
5. 保持 `main()` 中的 `CefRuntime::ExecuteSubprocess()`、`Initialize()`、
   `Shutdown()` 顺序不变；这是 CEF 多进程和安全退出的边界。

这个场景适合作为产品壳或调试壳继续扩展。若只是嵌入宿主程序，优先使用下面两个
公开 API 场景，不要直接依赖 `BrowserService`、`BrowserWidget` 或 `TabManager`。

## 场景二：WebView Browser 快速嵌入

`CefWebView` 是单页面控件，不包含地址栏和标签栏。宿主程序负责自己的业务 UI，
只把网页区域作为一个 QWidget 放入布局。

参考文件：

- `embedding_demo/webview/main.cpp`
- `src/offscreen_cef/cef_runtime.h`
- `src/offscreen_cef/cef_web_view.h`

CMake 接入：

```cmake
set(CEF_RUNTIME_LIBRARY_FLAG "/MD" CACHE STRING "CEF runtime library flag" FORCE)
add_subdirectory(third_party/offscreen-cef-browser)

target_link_libraries(host_app PRIVATE offscreen_cef::widgets)
offscreen_cef_deploy(host_app)
```

宿主启动步骤：

1. 在创建 `QApplication` 前调用 `offscreen::CefRuntime::ExecuteSubprocess()`。
2. 创建 `QApplication`。
3. 创建唯一的 `offscreen::CefRuntime`，并调用 `Initialize()`。
4. 创建宿主窗口，把 `offscreen::CefWebView` 加入布局或设为 central widget。
5. 窗口显示后调用 `LoadUrl(QUrl(...))`。
6. 关闭窗口时先调用 `CloseBrowser()`，等待 `browserClosed` 后再退出。
7. Qt 事件循环退出后调用 `runtime.Shutdown()`。

最小代码骨架：

```cpp
if (const auto exit_code = offscreen::CefRuntime::ExecuteSubprocess(
        ::GetModuleHandleW(nullptr))) {
  return *exit_code;
}

QApplication app(argc, argv);
offscreen::CefRuntime runtime;
if (!runtime.Initialize()) {
  return 1;
}

QMainWindow window;
auto* view = new offscreen::CefWebView(&window);
window.setCentralWidget(view);
window.show();
view->LoadUrl(QUrl("https://example.com/"));

const int result = app.exec();
runtime.CloseAllBrowsers();
return runtime.Shutdown() ? result : 1;
```

该骨架只展示启动和加载顺序。实际窗口关闭必须采用
`embedding_demo/webview/main.cpp` 的模式：先调用 `CloseBrowser()`，等待
`browserClosed`，再允许宿主窗口退出。正式宿主也建议显式传入稳定的
`CefRuntimeOptions::cache_path`。

跨线程收到 URL 指令时，用 `Qt::QueuedConnection` 投递到 `CefWebView::LoadUrl()`；
不要从工作线程直接调用 CEF 或 QWidget。

## 场景三：Tabbed Browser 快速嵌入

`CefTabbedBrowser` 是轻量多标签页容器。它内部创建多个 `CefWebView`，并把页面
popup 请求默认打开为新标签页。

参考文件：

- `embedding_demo/tabbed_browser/main.cpp`
- `src/offscreen_cef/cef_runtime.h`
- `src/offscreen_cef/cef_tabbed_browser.h`

CMake 接入与 WebView Browser 相同：

```cmake
set(CEF_RUNTIME_LIBRARY_FLAG "/MD" CACHE STRING "CEF runtime library flag" FORCE)
add_subdirectory(third_party/offscreen-cef-browser)

target_link_libraries(host_app PRIVATE offscreen_cef::widgets)
offscreen_cef_deploy(host_app)
```

宿主启动步骤：

1. 在 `QApplication` 前调用 `CefRuntime::ExecuteSubprocess()`。
2. 初始化唯一的 `CefRuntime`。
3. 创建 `offscreen::CefTabbedBrowser` 并加入宿主窗口。
4. 调用 `OpenTab(QUrl(...))` 打开首个页面。
5. 可订阅 `currentUrlChanged` 和 `currentTitleChanged` 同步宿主地址栏和标题。
6. 关闭窗口时调用 `CloseAllTabs()`，等待 `allTabsClosed` 后再退出。
7. Qt 事件循环退出后调用 `runtime.Shutdown()`。

最小代码骨架：

```cpp
offscreen::CefTabbedBrowser browser;
browser.OpenTab(QUrl("https://example.com/"));
browser.show();

QObject::connect(&browser, &offscreen::CefTabbedBrowser::allTabsClosed,
                 &app, &QApplication::quit);
```

该骨架只展示多标签页入口。实际窗口关闭必须采用
`embedding_demo/tabbed_browser/main.cpp` 的模式：先调用 `CloseAllTabs()`，等待
`allTabsClosed`，再允许宿主窗口退出。

如果宿主需要自己的导航栏，可通过 `CurrentView()` 取得当前页，再调用
`LoadUrl()`、`Reload()`、`Stop()` 或 `CloseBrowser()`。

## 交付检查

每个场景交付前至少确认：

- CMake 配置、构建和 CTest 通过。
- 对应 exe 能从 `bin\msvc-2022-x64\Release` 启动。
- 页面能显示、点击、输入、滚动并正常关闭。
- 关闭后没有残留 `offscreen_cef_browser.exe`、`embedding_demo_*.exe` 或
  `offscreen_cef_subprocess.exe` 进程。
- 嵌入宿主时使用稳定 `cache_path`，否则 LocalStorage、Cookie 和登录态会随重启丢失。
