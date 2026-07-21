# 嵌入宿主 Qt 程序

本模块面向 MSVC 2017、Qt 5.14.2、CEF 96 的 Windows 宿主程序。宿主进程
负责创建唯一的 `offscreen::CefRuntime`；每个页面使用 `CefWebView`，需要
多标签页时使用 `CefTabbedBrowser`。

## CMake 接入

将本项目作为子目录引入，并链接 Widgets 目标：

```cmake
add_subdirectory(third_party/offscreen-cef-browser)

target_link_libraries(host_app PRIVATE offscreen_cef::widgets)
offscreen_cef_deploy(host_app)
```

`offscreen_cef_deploy()` 会将 CEF runtime/resources 和
`offscreen_cef_subprocess.exe` 复制到宿主可执行文件目录。宿主与本模块必须
使用同一架构、MSVC 2017、Qt 5.14.2 和 CEF 96 binary distribution。

## 启动与退出

`CefRuntime::ExecuteSubprocess()` 必须在 `QApplication` 之前调用。所有
`CefWebView` 都关闭之后，才能调用 `Shutdown()`。

```cpp
int main(int argc, char* argv[]) {
  if (const auto exit_code = offscreen::CefRuntime::ExecuteSubprocess(
          ::GetModuleHandleW(nullptr))) {
    return *exit_code;
  }

  QApplication app(argc, argv);
  offscreen::CefRuntime runtime;
  if (!runtime.Initialize()) {
    return 1;
  }

  offscreen::CefTabbedBrowser browser;
  browser.OpenTab(QUrl("https://example.com/"));
  browser.show();

  QObject::connect(&browser, &offscreen::CefTabbedBrowser::allTabsClosed,
                   &app, &QApplication::quit);
  const int result = app.exec();
  runtime.CloseAllBrowsers();
  return runtime.Shutdown() ? result : 1;
}
```

## 全屏页面

全屏或指令驱动场景只需使用 `CefWebView`。跨线程收到 URL 指令时，以
`Qt::QueuedConnection` 投递 `LoadUrl()`，不要从工作线程直接调用 CEF。

```cpp
auto* view = new offscreen::CefWebView(this);
layout->addWidget(view);
connect(receiver, &Receiver::showUrl, view, &offscreen::CefWebView::LoadUrl,
        Qt::QueuedConnection);
```

`CefWebView::newWindowRequested` 代表页面 popup 请求；
`CefTabbedBrowser` 默认将它打开为新标签页。
