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
#include <QUrl>
#include <QVBoxLayout>

#include <windows.h>

#include "app/app_config.h"
#include "app/browser_app.h"
#include "app/browser_window_title.h"
#include "app/diagnostic_log.h"
#include "browser/browser_service.h"
#include "browser/tab_manager.h"
#include "offscreen_cef/cef_runtime.h"
#include "qt/browser_widget.h"

namespace {

/// 将 Qt 路径转换为 Windows 本地分隔符。
/// @param path 原始 Qt 路径。
/// @return 使用本地分隔符的路径。
QString NativePath(const QString& path) {
  return QDir::toNativeSeparators(path);
}

/// 将 UTF-8 标准字符串转换为 QString。
/// @param value UTF-8 文本。
/// @return 对应 QString。
QString StringToQString(const std::string& value) {
  return QString::fromUtf8(value.c_str(), static_cast<int>(value.size()));
}

/// 将 QString 编码为 UTF-8 标准字符串。
/// @param value Qt 文本。
/// @return UTF-8 文本。
std::string QStringToUtf8(const QString& value) {
  const QByteArray bytes = value.toUtf8();
  return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

/// 校验地址栏输入并规范化为允许协议的完整 URL。
/// @param input 用户输入文本。
/// @param normalized_url 输出规范化地址。
/// @return 输入有效时为 true。
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

/// 演示应用主窗口，负责地址栏、Qt 标签控件与 TabManager 的双向同步。
class TabbedBrowserWindow final : public QMainWindow {
 public:
  /// 创建主窗口和基础导航 UI。
  /// @param parent Qt 父控件。
  explicit TabbedBrowserWindow(QWidget* parent = nullptr);
  /// 连接标签管理器信号。
  /// @param tm 进程唯一的标签管理器。
  void SetTabManager(offscreen::TabManager* tm);
  /// 设置新建标签使用的默认地址。
  /// @param url 有效的 UTF-8 地址。
  void SetDefaultNewTabUrl(std::string url);

 protected:
  /// 在全部 CEF 标签关闭前阻止主窗口直接关闭。
  /// @param event Qt 关闭事件。
  void closeEvent(QCloseEvent* event) override;

 private:
  /// 将新建 BrowserWidget 插入 Qt 标签控件。
  /// @param id 新标签标识。
  /// @param w 新建浏览器控件。
  /// @param title 初始标题。
  void OnTabCreated(offscreen::TabManager::TabId id,
                    offscreen::BrowserWidget* w,
                    const std::string& title);
  /// 从 Qt 标签控件移除已清理标签。
  /// @param id 已关闭标签标识。
  void OnTabClosed(offscreen::TabManager::TabId id);
  /// 仅在变化标签为活动标签时刷新地址栏。
  /// @param id 变化标签标识。
  /// @param url 最新 UTF-8 地址。
  void OnTabAddressChanged(offscreen::TabManager::TabId id,
                           const std::string& url);
  /// 更新标签文字并同步活动窗口标题。
  /// @param id 变化标签标识。
  /// @param title 最新 UTF-8 标题。
  void OnTabTitleChanged(offscreen::TabManager::TabId id,
                         const std::string& title);
  /// 将 Qt 选中索引同步为活动浏览器标签。
  /// @param index 新选中标签索引。
  void OnTabSwitched(int index);
  /// 创建一个默认地址的新标签。
  void OnNewTabClicked();
  /// 校验地址栏文本并导航当前标签或创建首个标签。
  void OnUrlBarReturnPressed();

  QTabWidget* tab_widget_ = nullptr;
  QLineEdit* url_bar_ = nullptr;
  offscreen::TabManager* tab_manager_ = nullptr;
  std::string default_new_tab_url_ = "https://www.baidu.com";
};

/// 初始化主窗口布局、标签控件和地址栏事件连接。
/// @param parent Qt 父控件。
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

/// 保存管理器并订阅标签状态信号。
/// @param tm 进程唯一的标签管理器。
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

/// 更新默认新标签地址，忽略空地址。
/// @param url 新的 UTF-8 地址。
void TabbedBrowserWindow::SetDefaultNewTabUrl(std::string url) {
  if (!url.empty()) {
    default_new_tab_url_ = std::move(url);
  }
}

/// 向 TabManager 请求异步关闭全部标签，避免 CEF 生命周期被截断。
/// @param event Qt 关闭事件。
void TabbedBrowserWindow::closeEvent(QCloseEvent* event) {
  if (tab_manager_ && !tab_manager_->all_closed()) {
    tab_manager_->ShutdownAll();
    event->ignore();
    return;
  }
  QMainWindow::closeEvent(event);
}

/// 将创建完成的浏览器控件加入并激活 Qt 标签页。
/// @param id 新标签标识。
/// @param w 浏览器控件。
/// @param title 初始标题。
void TabbedBrowserWindow::OnTabCreated(offscreen::TabManager::TabId id,
                                       offscreen::BrowserWidget* w,
                                       const std::string& title) {
  (void)id;
  const int index = tab_widget_->addTab(w, StringToQString(title));
  tab_widget_->setCurrentIndex(index);
}

/// 查找并移除与标识匹配的 Qt 标签页。
/// @param id 已关闭标签标识。
void TabbedBrowserWindow::OnTabClosed(offscreen::TabManager::TabId id) {
  for (int i = 0; i < tab_widget_->count(); ++i) {
    auto* bw = qobject_cast<offscreen::BrowserWidget*>(tab_widget_->widget(i));
    if (bw && bw->tab_id() == id) {
      tab_widget_->removeTab(i);
      break;
    }
  }
}

/// 当活动标签地址变化且地址栏未编辑时更新显示。
/// @param id 变化标签标识。
/// @param url 最新 UTF-8 地址。
void TabbedBrowserWindow::OnTabAddressChanged(
    offscreen::TabManager::TabId id, const std::string& url) {
  const auto* active_bw = qobject_cast<offscreen::BrowserWidget*>(
      tab_widget_->currentWidget());
  if (active_bw && active_bw->tab_id() == id && !url_bar_->hasFocus()) {
    url_bar_->setText(StringToQString(url));
    url_bar_->setStyleSheet(QString());
  }
}

/// 更新标签文字，并在活动标签时更新窗口标题。
/// @param id 变化标签标识。
/// @param title 最新 UTF-8 标题。
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

/// 同步活动标签、地址栏、窗口标题和键盘焦点。
/// @param index 新选中标签索引。
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

/// 响应新建标签按钮。
void TabbedBrowserWindow::OnNewTabClicked() {
  if (tab_manager_) {
    tab_manager_->CreateTab(default_new_tab_url_);
  }
}

/// 响应地址栏回车，拒绝非法地址并导航有效地址。
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

/// 初始化日志、CEF、Qt 主窗口和标签管理器，再进入 Qt 事件循环。
/// @param argc 进程参数个数。
/// @param argv 进程参数数组。
/// @return 子进程退出码、Qt 事件循环结果或初始化失败码。
int main(int argc, char* argv[]) {
  offscreen::SetDiagnosticLogFileToApplicationDirectory();
  offscreen::DiagnosticLog("main entered");
  // CEF 子进程必须在创建 QApplication 前分流；子进程不进入宿主的 Qt 事件循环。
  if (const auto exit_code = offscreen::CefRuntime::ExecuteSubprocess(
          ::GetModuleHandleW(nullptr))) {
    offscreen::DiagnosticLog("CefRuntime::ExecuteSubprocess handled exit_code=" +
                             std::to_string(*exit_code));
    return *exit_code;
  }

  QApplication qt_app(argc, argv);

  const offscreen::AppConfig app_config =
      offscreen::AppConfig::FromArgs(argc, argv);
  offscreen::DiagnosticLog("AppConfig initial_url=[" + app_config.initial_url +
                           "]");

  const QString app_dir = QCoreApplication::applicationDirPath();
  offscreen::CefRuntime runtime;
  offscreen::CefRuntimeOptions runtime_options;
  runtime_options.subprocess_path = NativePath(
      QDir(app_dir).filePath("offscreen_cef_subprocess.exe")).toStdWString();
  runtime_options.cache_path =
      NativePath(QDir(app_dir).filePath("cef_cache")).toStdWString();
  runtime_options.log_path =
      NativePath(QDir(app_dir).filePath("cef.log")).toStdWString();
  // 主进程在创建任意标签页前完成一次进程级 CEF 初始化。
  if (!runtime.Initialize(runtime_options)) {
    offscreen::DiagnosticLog("CefRuntime::Initialize failed");
    return 1;
  }

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

  // 标签页关闭是异步的；若仍有视图登记，拒绝提前关闭 CEF 运行时。
  if (!runtime.Shutdown()) {
    offscreen::DiagnosticLog("CefRuntime::Shutdown deferred: browser still open");
    return 1;
  }
  return result;
}
