#include "offscreen_cef/cef_runtime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include "app/browser_app.h"
#include "app/diagnostic_log.h"
#include "offscreen_cef/cef_web_view.h"
#include "include/cef_app.h"

namespace offscreen {

namespace {

// 保存当前进程唯一的活动运行时，供嵌入控件登记和注销。
CefRuntime* g_active_runtime = nullptr;

/// 将宽字符串复制到 CEF 自管理字符串。
/// @param cef_string 接收结果的 CEF 字符串。
/// @param value 待复制的宽字符串。
void AssignCefString(cef_string_t* cef_string, const std::wstring& value) {
  cef_string_set(reinterpret_cast<const cef_char_t*>(value.data()),
                 value.size(), cef_string, true);
}

/// 判断 Win32 消息是否需要转交网页输入法处理。
/// @param message Win32 消息编号。
/// @return 输入法相关消息时为 true。
bool IsImeMessage(UINT message) {
  switch (message) {
    case WM_INPUTLANGCHANGE:
    case WM_IME_SETCONTEXT:
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_CHAR:
    case WM_IME_NOTIFY:
    case WM_IME_CONTROL:
    case WM_IME_COMPOSITIONFULL:
    case WM_IME_SELECT:
      return true;
    default:
      return false;
  }
}

/// 拼接当前可执行文件目录下的文件路径。
/// @param file_name 相对文件名。
/// @return 本地格式的绝对宽字符串路径。
std::wstring ApplicationFilePath(const wchar_t* file_name) {
  return QDir::toNativeSeparators(
             QDir(QCoreApplication::applicationDirPath()).filePath(
                 QString::fromWCharArray(file_name)))
      .toStdWString();
}

/// 将 CEF 可跨线程触发的外部消息泵请求合并为一个 Qt 单次定时器。
class CefMessagePumpScheduler final : public QObject {
 public:
  /// 创建依附于 Qt 应用对象的调度器。
  /// @param application 调度器的 Qt 父对象。
  explicit CefMessagePumpScheduler(QCoreApplication* application)
      : QObject(application) {
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this,
            [this]() { RunMessagePumpWork(); });
  }

  /// 记录最新执行期限并排队到 Qt 线程重启定时器。
  /// @param delay_ms CEF 要求的延迟毫秒数。
  void Schedule(int64_t delay_ms) {
    const int delay = static_cast<int>(std::min(
        std::max<int64_t>(delay_ms, 0),
        static_cast<int64_t>(std::numeric_limits<int>::max())));
    const auto deadline = Clock::now() + std::chrono::milliseconds(delay);
    bool queue_dispatch = false;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        return;
      }
      requested_deadline_ = deadline;
      if (!dispatch_queued_) {
        dispatch_queued_ = true;
        queue_dispatch = true;
      }
    }

    if (queue_dispatch &&
        !QMetaObject::invokeMethod(this, [this]() { ApplyPendingSchedule(); },
                                   Qt::QueuedConnection)) {
      std::lock_guard<std::mutex> lock(mutex_);
      dispatch_queued_ = false;
    }
  }

  /// 禁止后续调度并停止仍待触发的定时器。
  void Stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
      requested_deadline_ = Clock::time_point::max();
    }
    if (QThread::currentThread() == thread()) {
      timer_.stop();
    }
  }

 private:
  using Clock = std::chrono::steady_clock;

  /// 在 Qt 线程应用最新期限，覆盖旧的 CEF 调度请求。
  void ApplyPendingSchedule() {
    Clock::time_point deadline;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      dispatch_queued_ = false;
      if (stopping_) {
        return;
      }
      deadline = requested_deadline_;
      requested_deadline_ = Clock::time_point::max();
    }

    if (deadline == Clock::time_point::max()) {
      return;
    }

    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - Clock::now()).count();
    const int delay = static_cast<int>(std::min(
        std::max<decltype(remaining)>(remaining, 0),
        static_cast<decltype(remaining)>(std::numeric_limits<int>::max())));
    timer_.stop();
    timer_.start(delay);
  }

  /// 定时器触发时执行一次 CEF 消息泵工作。
  void RunMessagePumpWork() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        return;
      }
    }
    CefDoMessageLoopWork();
  }

  QTimer timer_;
  std::mutex mutex_;
  Clock::time_point requested_deadline_ = Clock::time_point::max();
  bool dispatch_queued_ = false;
  bool stopping_ = false;
};

}  // namespace

/// CefRuntime 的内部状态，负责将活动网页的原生 IME 消息路由给焦点控件。
class CefRuntime::Impl final : public QAbstractNativeEventFilter {
 public:
  /// 筛选并转发活动网页对应的 Win32 输入法消息。
  /// @param event_type Qt 原生事件类型。
  /// @param message Win32 MSG 指针。
  /// @param result 输出消息处理结果。
  /// @return 消息被网页控件处理时为 true。
  bool nativeEventFilter(const QByteArray& event_type, void* message,
                         qintptr* result) override {
    if ((event_type != "windows_generic_MSG" &&
         event_type != "windows_dispatcher_MSG") ||
        message == nullptr) {
      return false;
    }

    MSG* windows_message = static_cast<MSG*>(message);
    if (!IsImeMessage(windows_message->message)) {
      return false;
    }

    QWidget* focused = QApplication::focusWidget();
    for (const QPointer<CefWebView>& view : views) {
      if (view && focused && (focused == view || view->isAncestorOf(focused))) {
        return view->HandleNativeImeMessage(windows_message, result);
      }
    }
    return false;
  }

  CefRefPtr<BrowserApp> browser_app;
  std::unique_ptr<CefMessagePumpScheduler> message_pump_scheduler;
  std::unique_ptr<QTimer> fallback_message_pump_timer;
  std::vector<QPointer<CefWebView>> views;
  bool initialized = false;
};

/// 创建未初始化的运行时内部状态。
CefRuntime::CefRuntime() : impl_(std::make_unique<Impl>()) {}

/// 销毁运行时；遗漏 Shutdown 时尝试安全关闭。
CefRuntime::~CefRuntime() {
  if (IsInitialized()) {
    DiagnosticLog("CefRuntime destroyed before Shutdown");
    Shutdown();
  }
}

/// 将 CEF 子进程交给 CefExecuteProcess。
/// @param instance 当前模块实例句柄。
/// @return 子进程退出码，主进程继续执行时为空。
std::optional<int> CefRuntime::ExecuteSubprocess(HINSTANCE instance) {
  CefEnableHighDPISupport();
  CefMainArgs main_args(instance);
  CefRefPtr<BrowserApp> browser_app(new BrowserApp());
  const int exit_code = CefExecuteProcess(main_args, browser_app.get(), nullptr);
  if (exit_code >= 0) {
    return exit_code;
  }
  return std::nullopt;
}

/// 初始化 CEF、外部消息泵与原生 IME 过滤器。
/// @param options 子进程、缓存、日志路径；为空时使用应用目录默认值。
/// @return 初始化成功或已初始化时为 true。
bool CefRuntime::Initialize(const CefRuntimeOptions& options) {
  if (impl_->initialized) {
    return true;
  }
  if (g_active_runtime != nullptr || QCoreApplication::instance() == nullptr) {
    DiagnosticLog("CefRuntime::Initialize rejected: active runtime or no Qt application");
    return false;
  }

  impl_->browser_app = new BrowserApp();
  impl_->message_pump_scheduler = std::make_unique<CefMessagePumpScheduler>(
      QCoreApplication::instance());
  impl_->browser_app->SetMessagePumpScheduler(
      [scheduler = impl_->message_pump_scheduler.get()](int64_t delay_ms) {
        scheduler->Schedule(delay_ms);
      });

  CefSettings settings;
  settings.no_sandbox = true;
  settings.external_message_pump = true;
  settings.windowless_rendering_enabled = true;
  AssignCefString(&settings.browser_subprocess_path,
                  options.subprocess_path.empty()
                      ? ApplicationFilePath(L"offscreen_cef_subprocess.exe")
                      : options.subprocess_path);
  AssignCefString(&settings.cache_path,
                  options.cache_path.empty() ? ApplicationFilePath(L"cef_cache")
                                             : options.cache_path);
  AssignCefString(&settings.log_file,
                  options.log_path.empty() ? ApplicationFilePath(L"cef.log")
                                           : options.log_path);

  CefMainArgs main_args(::GetModuleHandleW(nullptr));
  if (!CefInitialize(main_args, settings, impl_->browser_app.get(), nullptr)) {
    DiagnosticLog("CefRuntime::Initialize CefInitialize failed");
    impl_->browser_app = nullptr;
    impl_->message_pump_scheduler.reset();
    return false;
  }

  impl_->fallback_message_pump_timer = std::make_unique<QTimer>();
  impl_->fallback_message_pump_timer->setInterval(10);
  QObject::connect(impl_->fallback_message_pump_timer.get(), &QTimer::timeout,
                   []() { CefDoMessageLoopWork(); });
  impl_->fallback_message_pump_timer->start();
  QCoreApplication::instance()->installNativeEventFilter(impl_.get());
  impl_->initialized = true;
  g_active_runtime = this;
  DiagnosticLog("CefRuntime::Initialize succeeded");
  return true;
}

/// 在没有登记网页时停止消息泵、移除过滤器并调用 CefShutdown。
/// @return 已停止或尚未初始化时为 true；仍有网页时为 false。
bool CefRuntime::Shutdown() {
  if (!impl_->initialized) {
    return true;
  }
  if (!AllBrowsersClosed()) {
    DiagnosticLog("CefRuntime::Shutdown rejected: browsers remain open");
    return false;
  }

  QCoreApplication::instance()->removeNativeEventFilter(impl_.get());
  impl_->message_pump_scheduler->Stop();
  impl_->fallback_message_pump_timer->stop();
  CefShutdown();
  impl_->fallback_message_pump_timer.reset();
  impl_->message_pump_scheduler.reset();
  impl_->browser_app = nullptr;
  impl_->initialized = false;
  g_active_runtime = nullptr;
  DiagnosticLog("CefRuntime::Shutdown completed");
  return true;
}

/// 查询运行时是否可创建浏览器。
/// @return CEF 初始化完成时为 true。
bool CefRuntime::IsInitialized() const {
  return impl_->initialized;
}

/// 查询已登记网页是否全部关闭或销毁。
/// @return 没有有效网页指针时为 true。
bool CefRuntime::AllBrowsersClosed() const {
  return std::none_of(impl_->views.begin(), impl_->views.end(),
                      [](const QPointer<CefWebView>& view) {
                        return !view.isNull();
                      });
}

/// 遍历登记列表并向每个有效网页请求异步关闭。
void CefRuntime::CloseAllBrowsers() {
  const auto views = impl_->views;
  for (const QPointer<CefWebView>& view : views) {
    if (view) {
      view->CloseBrowser();
    }
  }
}

/// 返回进程当前运行时实例。
/// @return 活动运行时；不存在时为 nullptr。
CefRuntime* CefRuntime::Active() {
  return g_active_runtime;
}

/// 将已创建 CEF 浏览器的网页加入运行时登记列表。
/// @param view 待登记的网页控件。
void CefRuntime::RegisterWebView(CefWebView* view) {
  if (!view || !impl_->initialized) {
    return;
  }
  const auto it = std::find_if(impl_->views.begin(), impl_->views.end(),
                               [view](const QPointer<CefWebView>& entry) {
                                 return entry == view;
                               });
  if (it == impl_->views.end()) {
    impl_->views.emplace_back(view);
  }
}

/// 从登记列表清理已关闭或已销毁的网页。
/// @param view 需要取消登记的网页控件。
void CefRuntime::UnregisterWebView(CefWebView* view) {
  impl_->views.erase(
      std::remove_if(impl_->views.begin(), impl_->views.end(),
                     [view](const QPointer<CefWebView>& entry) {
                       return entry.isNull() || entry == view;
                     }),
      impl_->views.end());
}

}  // namespace offscreen
