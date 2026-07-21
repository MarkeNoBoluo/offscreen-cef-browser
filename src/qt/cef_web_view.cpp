#include "offscreen_cef/cef_web_view.h"

#include <utility>

#include <QCloseEvent>
#include <QMetaObject>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include "app/diagnostic_log.h"
#include "browser/browser_ime_handler.h"
#include "browser/browser_service.h"
#include "offscreen_cef/cef_runtime.h"
#include "qt/browser_widget.h"

namespace offscreen {

namespace {

std::string UrlToUtf8(const QUrl& url) {
  const QByteArray bytes = url.toEncoded();
  return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

QUrl Utf8ToUrl(const std::string& value) {
  return QUrl::fromEncoded(QByteArray::fromStdString(value));
}

}  // namespace

CefWebView::CefWebView(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  browser_service_ = std::make_unique<BrowserService>();
  browser_widget_ = new BrowserWidget(this);
  browser_widget_->SetFrame(browser_service_->frame());
  browser_widget_->SetBrowserService(browser_service_.get());
  layout->addWidget(browser_widget_);

  browser_service_->SetBrowserClosedCallback(
      [this]() { OnBrowserClosed(); });
  browser_service_->SetPaintUpdateCallback(
      [this](const std::vector<BrowserViewRect>& dirty_rects) {
        browser_widget_->ScheduleFrameUpdate(dirty_rects);
      });
  browser_service_->SetCursorChangeCallback(
      [this](int cursor_type, HCURSOR cursor_handle) {
        QMetaObject::invokeMethod(
            this,
            [this, cursor_type, cursor_handle]() {
              if (browser_widget_) {
                browser_widget_->SetCefCursor(cursor_type, cursor_handle);
              }
            },
            Qt::QueuedConnection);
      });
  browser_service_->SetImeCompositionRangeChangedCallback(
      [this](const CefRange& selected_range,
             const std::vector<CefRect>& bounds) {
        if (browser_widget_) {
          browser_widget_->OnImeCompositionRangeChanged(selected_range, bounds);
        }
      });
  browser_service_->SetPopupRequestCallback([this](const std::string& url) {
    emit newWindowRequested(Utf8ToUrl(url));
  });
  browser_service_->SetAddressChangeCallback([this](const std::string& url) {
    emit urlChanged(Utf8ToUrl(url));
  });
  browser_service_->SetTitleChangeCallback([this](const std::string& title) {
    emit titleChanged(QString::fromUtf8(title.data(),
                                        static_cast<int>(title.size())));
  });
  browser_service_->SetLoadStateChangeCallback(
      [this](bool is_loading, bool, bool) {
        if (!is_loading) {
          emit loadFinished(browser_service_->last_error().empty());
        }
      });
  browser_service_->SetLoadErrorCallback([this](const std::string& error) {
    emit loadFailed(QString::fromUtf8(error.data(),
                                      static_cast<int>(error.size())));
  });
}

CefWebView::~CefWebView() {
  if (browser_registered_) {
    DiagnosticLog("CefWebView destroyed before browserClosed");
    if (CefRuntime* runtime = CefRuntime::Active()) {
      runtime->UnregisterWebView(this);
    }
  }
}

QUrl CefWebView::url() const {
  return browser_service_ ? Utf8ToUrl(browser_service_->address()) : QUrl();
}

QString CefWebView::title() const {
  if (!browser_service_) {
    return {};
  }
  const std::string value = browser_service_->title();
  return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

bool CefWebView::IsBrowserOpen() const {
  return browser_registered_ && browser_service_ && browser_service_->has_browser();
}

void CefWebView::LoadUrl(const QUrl& url) {
  if (!url.isValid() || url.scheme().isEmpty()) {
    emit loadFailed(QStringLiteral("URL must be absolute"));
    return;
  }
  close_requested_ = false;
  close_notified_ = false;
  pending_url_ = url;
  if (browser_service_ && browser_service_->has_browser()) {
    browser_service_->Navigate(UrlToUtf8(url));
    return;
  }
  StartBrowserIfReady();
}

void CefWebView::Reload() {
  if (browser_service_) {
    browser_service_->Reload();
  }
}

void CefWebView::Stop() {
  if (browser_service_) {
    browser_service_->Stop();
  }
}

void CefWebView::CloseBrowser() {
  if (!browser_service_) {
    return;
  }
  close_requested_ = true;
  if (!browser_create_requested_ && !browser_registered_) {
    OnBrowserClosed();
    return;
  }
  if (!browser_registered_) {
    return;
  }
  if (browser_service_->TryCloseBrowser()) {
    OnBrowserClosed();
  }
}

void CefWebView::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  StartBrowserIfReady();
}

void CefWebView::closeEvent(QCloseEvent* event) {
  if (browser_registered_) {
    CloseBrowser();
    event->ignore();
    return;
  }
  QWidget::closeEvent(event);
}

void CefWebView::StartBrowserIfReady() {
  if (browser_create_requested_ || !isVisible() || pending_url_.isEmpty()) {
    return;
  }
  if (CefRuntime::Active() == nullptr || !CefRuntime::Active()->IsInitialized()) {
    emit loadFailed(QStringLiteral("CefRuntime must be initialized before creating CefWebView"));
    return;
  }

  browser_create_requested_ = true;
  close_notified_ = false;
  QTimer::singleShot(0, this, [this]() {
    if (close_requested_) {
      OnBrowserClosed();
      return;
    }
    if (!browser_widget_ || !browser_service_ || pending_url_.isEmpty()) {
      return;
    }
    ime_handler_ = std::make_unique<BrowserImeHandler>(
        browser_widget_->NativeParentHandle());
    browser_widget_->SetImeHandler(ime_handler_.get());
    browser_widget_->SetResizeCallback(
        [service = browser_service_.get()](BrowserViewRect view_rect,
                                           double scale_factor) {
          service->Resize(view_rect, scale_factor);
        });

    if (!browser_service_->CreateBrowser(
            browser_widget_->NativeParentHandle(), browser_widget_->CurrentViewRect(),
            browser_widget_->CurrentDeviceScaleFactor(), UrlToUtf8(pending_url_))) {
      browser_create_requested_ = false;
      emit loadFailed(QStringLiteral("CefBrowserHost::CreateBrowser failed"));
      return;
    }
    browser_registered_ = true;
    CefRuntime::Active()->RegisterWebView(this);
    if (close_requested_) {
      CloseBrowser();
    }
  });
}

void CefWebView::OnBrowserClosed() {
  if (close_notified_) {
    return;
  }
  close_notified_ = true;
  browser_registered_ = false;
  browser_create_requested_ = false;
  close_requested_ = false;
  pending_url_ = QUrl();
  browser_widget_->SetResizeCallback({});
  browser_widget_->SetImeHandler(nullptr);
  ime_handler_.reset();
  if (CefRuntime* runtime = CefRuntime::Active()) {
    runtime->UnregisterWebView(this);
  }
  emit browserClosed();
}

bool CefWebView::HandleNativeImeMessage(MSG* message, long* result) {
  return browser_widget_ && browser_widget_->HandleImeNativeMessage(message, result);
}

}  // namespace offscreen
