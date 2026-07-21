#include "offscreen_cef/cef_runtime.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <utility>
#include <vector>

#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDir>
#include <QPointer>
#include <QTimer>

#include "app/browser_app.h"
#include "app/diagnostic_log.h"
#include "offscreen_cef/cef_web_view.h"
#include "include/cef_app.h"

namespace offscreen {

namespace {

CefRuntime* g_active_runtime = nullptr;

void AssignCefString(cef_string_t* cef_string, const std::wstring& value) {
  cef_string_set(reinterpret_cast<const cef_char_t*>(value.data()),
                 value.size(), cef_string, true);
}

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

std::wstring ApplicationFilePath(const wchar_t* file_name) {
  return QDir::toNativeSeparators(
             QDir(QCoreApplication::applicationDirPath()).filePath(
                 QString::fromWCharArray(file_name)))
      .toStdWString();
}

}  // namespace

class CefRuntime::Impl final : public QAbstractNativeEventFilter {
 public:
  bool nativeEventFilter(const QByteArray& event_type, void* message,
                         long* result) override {
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
  std::unique_ptr<QObject> message_pump_context;
  std::unique_ptr<QTimer> fallback_message_pump_timer;
  std::vector<QPointer<CefWebView>> views;
  bool initialized = false;
};

CefRuntime::CefRuntime() : impl_(std::make_unique<Impl>()) {}

CefRuntime::~CefRuntime() {
  if (IsInitialized()) {
    DiagnosticLog("CefRuntime destroyed before Shutdown");
    Shutdown();
  }
}

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

bool CefRuntime::Initialize(const CefRuntimeOptions& options) {
  if (impl_->initialized) {
    return true;
  }
  if (g_active_runtime != nullptr || QCoreApplication::instance() == nullptr) {
    DiagnosticLog("CefRuntime::Initialize rejected: active runtime or no Qt application");
    return false;
  }

  impl_->browser_app = new BrowserApp();
  impl_->message_pump_context = std::make_unique<QObject>();
  impl_->browser_app->SetMessagePumpScheduler(
      [context = impl_->message_pump_context.get()](int64_t delay_ms) {
        const int bounded_delay = static_cast<int>(std::min(
            std::max<int64_t>(delay_ms, 0),
            static_cast<int64_t>(std::numeric_limits<int>::max())));
        QTimer::singleShot(bounded_delay, context,
                           []() { CefDoMessageLoopWork(); });
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
    impl_->message_pump_context.reset();
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

bool CefRuntime::Shutdown() {
  if (!impl_->initialized) {
    return true;
  }
  if (!AllBrowsersClosed()) {
    DiagnosticLog("CefRuntime::Shutdown rejected: browsers remain open");
    return false;
  }

  QCoreApplication::instance()->removeNativeEventFilter(impl_.get());
  CefShutdown();
  impl_->fallback_message_pump_timer.reset();
  impl_->message_pump_context.reset();
  impl_->browser_app = nullptr;
  impl_->initialized = false;
  g_active_runtime = nullptr;
  DiagnosticLog("CefRuntime::Shutdown completed");
  return true;
}

bool CefRuntime::IsInitialized() const {
  return impl_->initialized;
}

bool CefRuntime::AllBrowsersClosed() const {
  return std::none_of(impl_->views.begin(), impl_->views.end(),
                      [](const QPointer<CefWebView>& view) {
                        return !view.isNull();
                      });
}

void CefRuntime::CloseAllBrowsers() {
  const auto views = impl_->views;
  for (const QPointer<CefWebView>& view : views) {
    if (view) {
      view->CloseBrowser();
    }
  }
}

CefRuntime* CefRuntime::Active() {
  return g_active_runtime;
}

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

void CefRuntime::UnregisterWebView(CefWebView* view) {
  impl_->views.erase(
      std::remove_if(impl_->views.begin(), impl_->views.end(),
                     [view](const QPointer<CefWebView>& entry) {
                       return entry.isNull() || entry == view;
                     }),
      impl_->views.end());
}

}  // namespace offscreen
