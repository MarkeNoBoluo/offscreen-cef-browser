# 嵌入宿主 Qt 程序

本模块面向 Windows x64、CEF 100、Qt 6.9.3 `msvc2022_64` 和 VS2022。宿主
进程负责创建唯一的 `offscreen::CefRuntime`；单页面嵌入使用
`offscreen::CefWebView`，需要多标签页时使用 `offscreen::CefTabbedBrowser`。

三种场景的完整交付说明见 [scenarios.md](scenarios.md)。本文只聚焦宿主程序
如何快速接入 WebView Browser 和 Tabbed Browser。

| 需求 | 推荐入口 |
| --- | --- |
| 独立最小浏览器外壳 | `offscreen_cef_browser.exe` / `src/app/main.cpp` |
| 单页面嵌入 | `offscreen::CefWebView` / `embedding_demo_webview.exe` |
| 多标签页嵌入 | `offscreen::CefTabbedBrowser` / `embedding_demo_tabbed_browser.exe` |

## CMake 接入

将本项目作为子目录引入，并链接 Widgets 目标：

```cmake
add_subdirectory(third_party/offscreen-cef-browser)

target_link_libraries(host_app PRIVATE offscreen_cef::widgets)
offscreen_cef_deploy(host_app)
```

`offscreen_cef_deploy()` 会将 CEF runtime/resources 和
`offscreen_cef_subprocess.exe` 复制到宿主可执行文件目录。宿主与本模块必须
使用同一架构、VS2022、Qt 6.9.3 `msvc2022_64`、CEF 100
`windows64_minimal` binary distribution，并统一使用 `/MD` 或 `/MDd`。

宿主工程在调用 `find_package(CEF)` 前必须设置：

```cmake
set(CEF_RUNTIME_LIBRARY_FLAG "/MD" CACHE STRING "CEF runtime library flag" FORCE)
```

否则 CEF wrapper 可能按默认运行库生成，导致 Qt、宿主和 CEF wrapper 的 CRT
配置不一致。

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

  const QString app_dir = QCoreApplication::applicationDirPath();
  offscreen::CefRuntimeOptions options;
  options.subprocess_path =
      QDir(app_dir).filePath("offscreen_cef_subprocess.exe").toStdWString();
  options.cache_path = QDir(app_dir).filePath("cef_cache").toStdWString();
  options.log_path = QDir(app_dir).filePath("cef.log").toStdWString();

  offscreen::CefRuntime runtime;
  if (!runtime.Initialize(options)) {
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

该片段展示进程初始化和关闭顺序；宿主窗口的实际关闭处理仍应等待
`browserClosed` 或 `allTabsClosed` 信号后再退出，示例代码见
`embedding_demo/webview/main.cpp` 和 `embedding_demo/tabbed_browser/main.cpp`。

`cache_path` 应保持稳定，否则 LocalStorage、Cookie 和登录态会随进程重启丢失。
生产环境不要默认使用临时 profile 或无痕 request context。

## WebView Browser 快速嵌入

全屏或指令驱动场景只需使用 `CefWebView`。跨线程收到 URL 指令时，以
`Qt::QueuedConnection` 投递 `LoadUrl()`，不要从工作线程直接调用 CEF。

接入步骤：

1. 宿主 CMake 链接 `offscreen_cef::widgets` 并调用 `offscreen_cef_deploy(host_app)`。
2. `main()` 里先执行 `CefRuntime::ExecuteSubprocess()`，再创建 `QApplication`。
3. 初始化唯一的 `CefRuntime`。
4. 创建 `CefWebView` 并加入宿主布局。
5. 窗口显示后调用 `LoadUrl()`。
6. 窗口关闭时调用 `CloseBrowser()`，等待 `browserClosed` 后再退出。
7. Qt 事件循环结束后调用 `Shutdown()`。

```cpp
auto* view = new offscreen::CefWebView(this);
layout->addWidget(view);
connect(receiver, &Receiver::showUrl, view, &offscreen::CefWebView::LoadUrl,
        Qt::QueuedConnection);
```

`CefWebView::newWindowRequested` 代表页面 popup 请求；
`CefTabbedBrowser` 默认将它打开为新标签页。

完整示例见 `embedding_demo/webview/main.cpp`。

## Tabbed Browser 快速嵌入

多页面浏览场景使用 `CefTabbedBrowser`。它内部管理多个 `CefWebView`，并提供
当前标签地址、标题和全部标签关闭信号。

接入步骤：

1. CMake 接入方式与 WebView Browser 相同。
2. `main()` 中保持 `ExecuteSubprocess()`、`QApplication`、`CefRuntime::Initialize()` 顺序。
3. 创建 `CefTabbedBrowser` 并加入宿主窗口。
4. 调用 `OpenTab(QUrl(...))` 打开首个页面。
5. 订阅 `currentUrlChanged`、`currentTitleChanged` 同步宿主工具栏。
6. 窗口关闭时调用 `CloseAllTabs()`，等待 `allTabsClosed` 后再退出。
7. Qt 事件循环结束后调用 `Shutdown()`。

```cpp
auto* browser = new offscreen::CefTabbedBrowser(this);
layout->addWidget(browser);
browser->OpenTab(QUrl("https://example.com/"));
```

如果宿主需要自己的导航按钮，可通过 `CurrentView()` 取得当前页，再调用
`LoadUrl()`、`Reload()`、`Stop()` 或 `CloseBrowser()`。

完整示例见 `embedding_demo/tabbed_browser/main.cpp`。

## 本地兼容性验证页

`embedding_demo_webview.exe` 支持把第一个参数作为本地 `.html` 文件加载：

```powershell
& ".\bin\msvc-2022-x64\Release\embedding_demo_webview.exe" `
  ".\tests\visualization_compatibility_test.html"
```

该入口用于验证 Canvas、LocalStorage、Fetch、WebGL、IndexedDB、Web Worker、
Video codec 能力探测，以及 Clipboard、Drag & Drop、File API 等需要人工操作的
设计器能力。Cookie、登录态、WebSocket 和真实业务 API 仍需用目标服务地址做
现场验收。
## 宿主控制下载确认

默认的 `DownloadDecisionMode::kAutomatic` 与此前的下载行为兼容：CEF 按已有下载目录和建议文件名继续下载。只有在宿主需要向用户确认保存位置或拒绝下载时，才使用 `DownloadDecisionMode::kAskHost`。

先连接 `downloadRequested`，再设置 `kAskHost`。在 ask-host 模式下，每个信号都代表一个待决下载；你必须调用 `AcceptDownload()` 或 `CancelDownload()` 处理它。`AcceptDownload()` 的 `fullPath` 必须是含文件名的绝对完整路径。传入空路径、相对路径或没有文件名的路径会按取消处理。

`CancelDownload()` 只释放 CEF 提供的 before-download callback。CEF 的该 callback 没有可调用的取消成员函数，因此不要尝试对 callback 调用 `Cancel()`。取消请求也不保证立即收到 `downloadStateChanged(2, ...)`；应将该信号视为异步状态通知。

使用非静态 `QFileDialog` 并调用异步 `open()`，不要使用 `QFileDialog::getSaveFileName()` 等同步静态函数。页面或标签关闭、或从 `kAskHost` 切换到其他模式时，待决请求会失效；为每个 lambda 指定 Qt context object，并使用 `QPointer` 防止异步对话框回调访问已销毁的页面。

### 单页 WebView

以下最小示例先连接信号，再切换模式。`fileSelected` 给出的路径在传递前经过绝对路径检查，取消对话框会释放待决 callback。

```cpp
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>
#include <QStandardPaths>

// view 是已创建并处于布局中的 offscreen::CefWebView*。
connect(view, &offscreen::CefWebView::downloadRequested, view,
        [guarded_view = QPointer<offscreen::CefWebView>(view)](
            offscreen::DownloadRequestId id, const QString& suggested_name,
            const QUrl&) {
          if (!guarded_view) {
            return;
          }

          auto* dialog = new QFileDialog(guarded_view);
          dialog->setAcceptMode(QFileDialog::AcceptSave);
          dialog->setFileMode(QFileDialog::AnyFile);
          dialog->setConfirmOverwrite(true);
          dialog->setDirectory(QStandardPaths::writableLocation(
              QStandardPaths::DownloadLocation));
          dialog->selectFile(suggested_name);
          dialog->setAttribute(Qt::WA_DeleteOnClose);
          connect(dialog, &QFileDialog::fileSelected, guarded_view,
                  [guarded_view, id](const QString& full_path) {
                    if (!guarded_view) {
                      return;
                    }
                    if (QFileInfo(full_path).isAbsolute() &&
                        !QFileInfo(full_path).fileName().isEmpty()) {
                      guarded_view->AcceptDownload(id, full_path);
                    } else {
                      guarded_view->CancelDownload(id);
                    }
                  });
          connect(dialog, &QDialog::rejected, guarded_view,
                  [guarded_view, id]() {
                    if (guarded_view) {
                      guarded_view->CancelDownload(id);
                    }
                  });
          dialog->open();
        });
view->SetDownloadDecisionMode(offscreen::DownloadDecisionMode::kAskHost);
```

### 多标签页 CefTabbedBrowser

`CefTabbedBrowser::downloadRequested` 的第一个参数是发起下载的 `CefWebView*`。始终使用这个 signal 参数处理请求，而不要调用 `CurrentView()`；下载可能来自后台标签页或 popup 标签页，而当前活动标签页可能不同。

```cpp
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>
#include <QStandardPaths>

// browser 是已创建的 offscreen::CefTabbedBrowser*。
connect(browser, &offscreen::CefTabbedBrowser::downloadRequested, browser,
        [](offscreen::CefWebView* view, offscreen::DownloadRequestId id,
           const QString& suggested_name, const QUrl&) {
          QPointer<offscreen::CefWebView> guarded_view(view);
          if (!guarded_view) {
            return;
          }

          auto* dialog = new QFileDialog(guarded_view);
          dialog->setAcceptMode(QFileDialog::AcceptSave);
          dialog->setFileMode(QFileDialog::AnyFile);
          dialog->setConfirmOverwrite(true);
          dialog->setDirectory(QStandardPaths::writableLocation(
              QStandardPaths::DownloadLocation));
          dialog->selectFile(suggested_name);
          dialog->setAttribute(Qt::WA_DeleteOnClose);
          connect(dialog, &QFileDialog::fileSelected, guarded_view,
                  [guarded_view, id](const QString& full_path) {
                    if (!guarded_view) {
                      return;
                    }
                    if (QFileInfo(full_path).isAbsolute() &&
                        !QFileInfo(full_path).fileName().isEmpty()) {
                      guarded_view->AcceptDownload(id, full_path);
                    } else {
                      guarded_view->CancelDownload(id);
                    }
                  });
          connect(dialog, &QDialog::rejected, guarded_view,
                  [guarded_view, id]() {
                    if (guarded_view) {
                      guarded_view->CancelDownload(id);
                    }
                  });
          dialog->open();
        });
browser->SetDownloadDecisionMode(offscreen::DownloadDecisionMode::kAskHost);
```
