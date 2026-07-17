#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QLineEdit>
#include <QMainWindow>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QString>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <windows.h>

#include "app/app_config.h"
#include "app/browser_app.h"
#include "app/browser_window_title.h"
#include "app/diagnostic_log.h"
#include "browser/browser_service.h"
#include "browser/tab_manager.h"
#include "include/cef_app.h"
#include "qt/browser_widget.h"

namespace {

QString NativePath(const QString& path) {
  return QDir::toNativeSeparators(path);
}

QString StringToQString(const std::string& value) {
  return QString::fromUtf8(value.c_str(), static_cast<int>(value.size()));
}

std::string QStringToUtf8(const QString& value) {
  const QByteArray bytes = value.toUtf8();
  return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

bool TryNormalizeAddressBarUrl(const QString& input, QString* normalized_url) {
  const QString trimmed = input.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }

  const QUrl url(trimmed, QUrl::StrictMode);
  const QString scheme = url.scheme().toLower();
  if (!url.isValid() || scheme.isEmpty()) {
    return false;
  }
  if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https") &&
      scheme != QStringLiteral("file") && scheme != QStringLiteral("about")) {
    return false;
  }
  if ((scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) &&
      url.host().isEmpty()) {
    return false;
  }

  *normalized_url = url.toString(QUrl::FullyEncoded);
  return true;
}

void AssignCefString(cef_string_t* cef_string, const QString& value) {
  cef_string_set(reinterpret_cast<const cef_char_t*>(value.utf16()),
                 static_cast<size_t>(value.size()), cef_string, true);
}

class TabbedBrowserWindow final : public QMainWindow {
 public:
  explicit TabbedBrowserWindow(QWidget* parent = nullptr);
  void SetTabManager(offscreen::TabManager* tm);
  void SetDefaultNewTabUrl(std::string url);

 protected:
  void closeEvent(QCloseEvent* event) override;

 private:
  void OnTabCreated(offscreen::TabManager::TabId id,
                    offscreen::BrowserWidget* w,
                    const std::string& title);
  void OnTabClosed(offscreen::TabManager::TabId id);
  void OnTabAddressChanged(offscreen::TabManager::TabId id,
                           const std::string& url);
  void OnTabTitleChanged(offscreen::TabManager::TabId id,
                         const std::string& title);
  void OnTabSwitched(int index);
  void OnNewTabClicked();
  void OnUrlBarReturnPressed();

  QTabWidget* tab_widget_ = nullptr;
  QLineEdit* url_bar_ = nullptr;
  offscreen::TabManager* tab_manager_ = nullptr;
  std::string default_new_tab_url_ = "https://www.baidu.com";
};

TabbedBrowserWindow::TabbedBrowserWindow(QWidget* parent)
    : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("Offscreen CEF Browser"));

  auto* central = new QWidget(this);
  auto* layout = new QVBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  url_bar_ = new QLineEdit(central);
  url_bar_->setPlaceholderText(QStringLiteral("Enter URL and press Enter..."));
  QObject::connect(url_bar_, &QLineEdit::returnPressed, this,
                   &TabbedBrowserWindow::OnUrlBarReturnPressed);
  QObject::connect(url_bar_, &QLineEdit::textEdited, this,
                   [this]() { url_bar_->setStyleSheet(QString()); });
  layout->addWidget(url_bar_);

  tab_widget_ = new QTabWidget(central);
  tab_widget_->setTabsClosable(true);
  tab_widget_->setMovable(true);
  QObject::connect(tab_widget_, &QTabWidget::currentChanged, this,
                   &TabbedBrowserWindow::OnTabSwitched);
  QObject::connect(tab_widget_, &QTabWidget::tabCloseRequested, this,
                   [this](int index) {
                     auto* w = tab_widget_->widget(index);
                     if (!w) return;
                     auto* bw = qobject_cast<offscreen::BrowserWidget*>(w);
                     if (!bw) return;
                     const auto id = bw->tab_id();
                     if (id != offscreen::TabManager::kInvalidTabId) {
                       tab_manager_->RequestCloseTab(id);
                     }
                   });
  layout->addWidget(tab_widget_);

  auto* new_tab_btn = new QPushButton(QStringLiteral("+"), central);
  new_tab_btn->setFixedSize(24, 24);
  new_tab_btn->setToolTip(QStringLiteral("New Tab"));
  QObject::connect(new_tab_btn, &QPushButton::clicked, this,
                   &TabbedBrowserWindow::OnNewTabClicked);
  tab_widget_->setCornerWidget(new_tab_btn, Qt::TopRightCorner);

  setCentralWidget(central);
}

void TabbedBrowserWindow::SetTabManager(offscreen::TabManager* tm) {
  tab_manager_ = tm;
  QObject::connect(tab_manager_, &offscreen::TabManager::TabCreated, this,
                   &TabbedBrowserWindow::OnTabCreated);
  QObject::connect(tab_manager_, &offscreen::TabManager::TabClosed, this,
                   &TabbedBrowserWindow::OnTabClosed);
  QObject::connect(tab_manager_, &offscreen::TabManager::TabAddressChanged, this,
                   &TabbedBrowserWindow::OnTabAddressChanged);
  QObject::connect(tab_manager_, &offscreen::TabManager::TabTitleChanged, this,
                   &TabbedBrowserWindow::OnTabTitleChanged);
  QObject::connect(tab_manager_, &offscreen::TabManager::AllTabsClosed, this,
                   [this]() { close(); });
}

void TabbedBrowserWindow::SetDefaultNewTabUrl(std::string url) {
  if (!url.empty()) {
    default_new_tab_url_ = std::move(url);
  }
}

void TabbedBrowserWindow::closeEvent(QCloseEvent* event) {
  if (tab_manager_ && !tab_manager_->all_closed()) {
    tab_manager_->ShutdownAll();
    event->ignore();
    return;
  }
  QMainWindow::closeEvent(event);
}

void TabbedBrowserWindow::OnTabCreated(offscreen::TabManager::TabId id,
                                       offscreen::BrowserWidget* w,
                                       const std::string& title) {
  (void)id;
  const int index = tab_widget_->addTab(w, StringToQString(title));
  tab_widget_->setCurrentIndex(index);
}

void TabbedBrowserWindow::OnTabClosed(offscreen::TabManager::TabId id) {
  for (int i = 0; i < tab_widget_->count(); ++i) {
    auto* bw = qobject_cast<offscreen::BrowserWidget*>(tab_widget_->widget(i));
    if (bw && bw->tab_id() == id) {
      tab_widget_->removeTab(i);
      break;
    }
  }
}

void TabbedBrowserWindow::OnTabAddressChanged(
    offscreen::TabManager::TabId id, const std::string& url) {
  const auto* active_bw = qobject_cast<offscreen::BrowserWidget*>(
      tab_widget_->currentWidget());
  if (active_bw && active_bw->tab_id() == id && !url_bar_->hasFocus()) {
    url_bar_->setText(StringToQString(url));
    url_bar_->setStyleSheet(QString());
  }
}

void TabbedBrowserWindow::OnTabTitleChanged(
    offscreen::TabManager::TabId id, const std::string& title) {
  for (int i = 0; i < tab_widget_->count(); ++i) {
    auto* bw = qobject_cast<offscreen::BrowserWidget*>(tab_widget_->widget(i));
    if (bw && bw->tab_id() == id) {
      tab_widget_->setTabText(i, StringToQString(title));
      const auto* active_bw = qobject_cast<offscreen::BrowserWidget*>(
          tab_widget_->currentWidget());
      if (active_bw && active_bw->tab_id() == id) {
        setWindowTitle(QStringLiteral("Offscreen CEF Browser - %1")
                           .arg(StringToQString(title)));
      }
      break;
    }
  }
}

void TabbedBrowserWindow::OnTabSwitched(int index) {
  auto* w = tab_widget_->widget(index);
  if (!w) return;
  auto* bw = qobject_cast<offscreen::BrowserWidget*>(w);
  if (!bw) return;
  const auto id = bw->tab_id();
  if (id != offscreen::TabManager::kInvalidTabId) {
    tab_manager_->SetActiveTab(id);
  }
  auto* service = tab_manager_->ActiveBrowserService();
  if (service) {
    url_bar_->setText(StringToQString(service->address()));
    url_bar_->setStyleSheet(QString());
    setWindowTitle(QStringLiteral("Offscreen CEF Browser - %1")
                       .arg(StringToQString(service->title())));
  }
  if (bw) bw->setFocus();
}

void TabbedBrowserWindow::OnNewTabClicked() {
  if (tab_manager_) {
    tab_manager_->CreateTab(default_new_tab_url_);
  }
}

void TabbedBrowserWindow::OnUrlBarReturnPressed() {
  QString normalized_url;
  if (!TryNormalizeAddressBarUrl(url_bar_->text(), &normalized_url)) {
    offscreen::DiagnosticLog("TabbedBrowserWindow::OnUrlBarReturnPressed "
                             "rejected invalid url");
    url_bar_->setStyleSheet(QStringLiteral(
        "QLineEdit { border: 1px solid #c62828; }"));
    url_bar_->selectAll();
    return;
  }

  const std::string url = QStringToUtf8(normalized_url);
  auto* service = tab_manager_->ActiveBrowserService();
  url_bar_->setText(normalized_url);
  url_bar_->setStyleSheet(QString());
  if (!service) {
    tab_manager_->CreateTab(url);
    return;
  }
  if (!url.empty()) {
    service->Navigate(url);
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  offscreen::SetDiagnosticLogFileToApplicationDirectory();
  offscreen::DiagnosticLog("main entered");
  CefEnableHighDPISupport();

  CefMainArgs main_args(::GetModuleHandleW(nullptr));

  CefRefPtr<offscreen::BrowserApp> cef_app(new offscreen::BrowserApp());

  const int exit_code = CefExecuteProcess(main_args, cef_app.get(), nullptr);
  if (exit_code >= 0) {
    offscreen::DiagnosticLog("CefExecuteProcess handled subprocess exit_code=" +
                             std::to_string(exit_code));
    return exit_code;
  }
  offscreen::DiagnosticLog("CefExecuteProcess returned browser-process path");

  QApplication qt_app(argc, argv);
  QObject message_pump_context;
  cef_app->SetMessagePumpScheduler([&message_pump_context](int64_t delay_ms) {
    const int64_t bounded_delay =
        std::min(std::max<int64_t>(delay_ms, 0),
                 static_cast<int64_t>(std::numeric_limits<int>::max()));
    QTimer::singleShot(static_cast<int>(bounded_delay), &message_pump_context,
                       []() { CefDoMessageLoopWork(); });
  });

  const offscreen::AppConfig app_config =
      offscreen::AppConfig::FromArgs(argc, argv);
  offscreen::DiagnosticLog("AppConfig initial_url=[" + app_config.initial_url +
                           "]");

  offscreen::DiagnosticLog("Resolving runtime paths");
  const QString app_dir = QCoreApplication::applicationDirPath();
  const QString subprocess_path =
      NativePath(QDir(app_dir).filePath("offscreen_cef_subprocess.exe"));
  const QString cache_path = NativePath(QDir(app_dir).filePath("cef_cache"));
  const QString log_path = NativePath(QDir(app_dir).filePath("cef.log"));

  CefSettings settings;
  settings.no_sandbox = true;
  settings.external_message_pump = true;
  settings.windowless_rendering_enabled = true;
  AssignCefString(&settings.browser_subprocess_path, subprocess_path);
  AssignCefString(&settings.cache_path, cache_path);
  AssignCefString(&settings.log_file, log_path);

  {
    std::ostringstream stream;
    stream << "CefSettings no_sandbox=" << settings.no_sandbox
           << " external_message_pump=" << settings.external_message_pump
           << " windowless_rendering_enabled="
           << settings.windowless_rendering_enabled;
    offscreen::DiagnosticLog(stream.str());
  }

  if (!CefInitialize(main_args, settings, cef_app.get(), nullptr)) {
    offscreen::DiagnosticLog("CefInitialize failed");
    return 1;
  }
  offscreen::DiagnosticLog("CefInitialize succeeded");

  QTimer cef_work_timer;
  cef_work_timer.setInterval(10);
  QObject::connect(&cef_work_timer, &QTimer::timeout, []() {
    CefDoMessageLoopWork();
  });
  cef_work_timer.start();

  TabbedBrowserWindow main_window;
  main_window.SetDefaultNewTabUrl(app_config.initial_url);
  main_window.resize(1024, 768);

  auto* tab_manager = new offscreen::TabManager(&main_window);
  main_window.SetTabManager(tab_manager);

  qt_app.installNativeEventFilter(tab_manager);

  tab_manager->CreateTab(app_config.initial_url);

  main_window.show();

  const int result = qt_app.exec();
  offscreen::DiagnosticLog("Qt event loop exited result=" +
                           std::to_string(result));

  CefShutdown();
  offscreen::DiagnosticLog("CefShutdown completed");
  return result;
}
