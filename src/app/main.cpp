#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
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
  /// 后退按钮响应。
  void OnBackClicked();
  /// 前进按钮响应。
  void OnForwardClicked();
  /// 刷新按钮响应。
  void OnReloadClicked();
  /// 停止按钮响应。
  void OnStopClicked();
  /// 构建并显示右键菜单。
  /// @param w 被点击的浏览器控件。
  /// @param global_pos 菜单弹出的全局坐标。
  void OnContextMenuRequested(offscreen::BrowserWidget* w,
                              const QPoint& global_pos);
  /// 刷新后退/前进/停止按钮可用状态。
  /// @param is_loading 是否正在加载。
  /// @param can_go_back 是否可后退。
  /// @param can_go_forward 是否可前进。
  void UpdateNavigationButtons(bool is_loading, bool can_go_back,
                               bool can_go_forward);
  /// 仅活动标签的加载状态变化时更新按钮和状态栏。
  /// @param id 变化标签标识。
  /// @param is_loading 是否正在加载。
  /// @param can_go_back 是否可后退。
  /// @param can_go_forward 是否可前进。
  void OnTabLoadStateChanged(offscreen::TabManager::TabId id,
                             bool is_loading, bool can_go_back,
                             bool can_go_forward);
  /// 仅活动标签的加载错误时更新状态栏。
  /// @param id 变化标签标识。
  /// @param error_code CEF 错误码。
  /// @param failed_url 失败的 UTF-8 地址。
  /// @param error_text UTF-8 错误说明。
  void OnTabLoadError(offscreen::TabManager::TabId id, int error_code,
                      const std::string& failed_url,
                      const std::string& error_text);
  /// 仅活动标签的下载状态变化时更新状态栏。
  /// @param id 下载所属标签。
  /// @param state 下载状态：0 开始、1 完成、2 取消。
  /// @param file_name UTF-8 文件名。
  /// @param full_path UTF-8 完整保存路径。
  void OnTabDownloadStateChanged(offscreen::TabManager::TabId id, int state,
                                 const std::string& file_name,
                                 const std::string& full_path);

  QTabWidget* tab_widget_ = nullptr;
  QLineEdit* url_bar_ = nullptr;
  QPushButton* back_button_ = nullptr;
  QPushButton* forward_button_ = nullptr;
  QPushButton* reload_button_ = nullptr;
  QPushButton* stop_button_ = nullptr;
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

  auto* nav_bar = new QHBoxLayout();
  nav_bar->setContentsMargins(0, 0, 0, 0);
  nav_bar->setSpacing(2);

  back_button_ = new QPushButton(QStringLiteral("\u2190"), central);
  back_button_->setEnabled(false);
  back_button_->setToolTip(QStringLiteral("Back"));
  QObject::connect(back_button_, &QPushButton::clicked, this,
                   &TabbedBrowserWindow::OnBackClicked);
  nav_bar->addWidget(back_button_);

  forward_button_ = new QPushButton(QStringLiteral("\u2192"), central);
  forward_button_->setEnabled(false);
  forward_button_->setToolTip(QStringLiteral("Forward"));
  QObject::connect(forward_button_, &QPushButton::clicked, this,
                   &TabbedBrowserWindow::OnForwardClicked);
  nav_bar->addWidget(forward_button_);

  reload_button_ = new QPushButton(QStringLiteral("\u21bb"), central);
  reload_button_->setToolTip(QStringLiteral("Reload"));
  QObject::connect(reload_button_, &QPushButton::clicked, this,
                   &TabbedBrowserWindow::OnReloadClicked);
  nav_bar->addWidget(reload_button_);

  stop_button_ = new QPushButton(QStringLiteral("\u2715"), central);
  stop_button_->setEnabled(false);
  stop_button_->setToolTip(QStringLiteral("Stop"));
  QObject::connect(stop_button_, &QPushButton::clicked, this,
                   &TabbedBrowserWindow::OnStopClicked);
  nav_bar->addWidget(stop_button_);

  url_bar_ = new QLineEdit(central);
  url_bar_->setPlaceholderText(QStringLiteral("Enter URL and press Enter..."));
  QObject::connect(url_bar_, &QLineEdit::returnPressed, this,
                   &TabbedBrowserWindow::OnUrlBarReturnPressed);
  QObject::connect(url_bar_, &QLineEdit::textEdited, this,
                   [this]() { url_bar_->setStyleSheet(QString()); });
  nav_bar->addWidget(url_bar_, 1);

  layout->addLayout(nav_bar);

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
  QObject::connect(tab_manager_, &offscreen::TabManager::TabLoadStateChanged,
                   this, &TabbedBrowserWindow::OnTabLoadStateChanged);
  QObject::connect(tab_manager_, &offscreen::TabManager::TabLoadError, this,
                   &TabbedBrowserWindow::OnTabLoadError);
  QObject::connect(tab_manager_, &offscreen::TabManager::TabDownloadStateChanged,
                   this, &TabbedBrowserWindow::OnTabDownloadStateChanged);
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
  QObject::connect(w, &offscreen::BrowserWidget::contextMenuRequested, this,
                   [this, w](const QPoint& pos) {
                     OnContextMenuRequested(w, pos);
                   });
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
    UpdateNavigationButtons(service->is_loading(), service->can_go_back(),
                            service->can_go_forward());
    if (service->is_loading()) {
      statusBar()->showMessage(QStringLiteral("Loading %1...")
                                   .arg(StringToQString(service->address())));
    } else {
      statusBar()->clearMessage();
    }
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

/// 后退当前活动标签。
void TabbedBrowserWindow::OnBackClicked() {
  auto* service = tab_manager_->ActiveBrowserService();
  if (service) {
    service->GoBack();
  }
}

/// 前进当前活动标签。
void TabbedBrowserWindow::OnForwardClicked() {
  auto* service = tab_manager_->ActiveBrowserService();
  if (service) {
    service->GoForward();
  }
}

/// 刷新当前活动标签。
void TabbedBrowserWindow::OnReloadClicked() {
  auto* service = tab_manager_->ActiveBrowserService();
  if (service) {
    service->Reload();
  }
}

/// 停止当前活动标签加载。
void TabbedBrowserWindow::OnStopClicked() {
  auto* service = tab_manager_->ActiveBrowserService();
  if (service) {
    service->Stop();
  }
}

/// 为被点击标签构建导航与编辑右键菜单。
/// @param w 被点击的浏览器控件。
/// @param global_pos 菜单弹出的全局坐标。
void TabbedBrowserWindow::OnContextMenuRequested(
    offscreen::BrowserWidget* w, const QPoint& global_pos) {
  auto* service = tab_manager_->BrowserServiceForWidget(w);
  if (!service) {
    return;
  }
  QMenu menu(this);
  QAction* back_action = menu.addAction(QStringLiteral("Back"));
  back_action->setEnabled(service->can_go_back());
  QAction* forward_action = menu.addAction(QStringLiteral("Forward"));
  forward_action->setEnabled(service->can_go_forward());
  QAction* reload_action = menu.addAction(QStringLiteral("Reload"));
  QAction* stop_action = menu.addAction(QStringLiteral("Stop"));
  stop_action->setEnabled(service->is_loading());
  menu.addSeparator();
  QAction* copy_action = menu.addAction(QStringLiteral("Copy"));
  QAction* cut_action = menu.addAction(QStringLiteral("Cut"));
  QAction* paste_action = menu.addAction(QStringLiteral("Paste"));
  QAction* select_all_action = menu.addAction(QStringLiteral("Select All"));

  QAction* selected = menu.exec(global_pos);
  if (!selected) {
    return;
  }
  if (selected == back_action) {
    service->GoBack();
  } else if (selected == forward_action) {
    service->GoForward();
  } else if (selected == reload_action) {
    service->Reload();
  } else if (selected == stop_action) {
    service->Stop();
  } else if (selected == copy_action) {
    service->Copy();
  } else if (selected == cut_action) {
    service->Cut();
  } else if (selected == paste_action) {
    service->Paste();
  } else if (selected == select_all_action) {
    service->SelectAll();
  }
}

/// 根据加载与历史状态刷新导航按钮。
/// @param is_loading 是否正在加载。
/// @param can_go_back 是否可后退。
/// @param can_go_forward 是否可前进。
void TabbedBrowserWindow::UpdateNavigationButtons(bool is_loading,
                                                  bool can_go_back,
                                                  bool can_go_forward) {
  back_button_->setEnabled(can_go_back);
  forward_button_->setEnabled(can_go_forward);
  stop_button_->setEnabled(is_loading);
}

/// 仅当变化标签为活动标签时刷新按钮与状态栏。
/// @param id 变化标签标识。
/// @param is_loading 是否正在加载。
/// @param can_go_back 是否可后退。
/// @param can_go_forward 是否可前进。
void TabbedBrowserWindow::OnTabLoadStateChanged(
    offscreen::TabManager::TabId id, bool is_loading, bool can_go_back,
    bool can_go_forward) {
  auto* active_bw = qobject_cast<offscreen::BrowserWidget*>(
      tab_widget_->currentWidget());
  if (!active_bw || active_bw->tab_id() != id) {
    return;
  }
  UpdateNavigationButtons(is_loading, can_go_back, can_go_forward);
  auto* service = tab_manager_->ActiveBrowserService();
  if (is_loading) {
    statusBar()->showMessage(QStringLiteral("Loading %1...")
                                 .arg(StringToQString(
                                     service ? service->address()
                                             : std::string())));
  } else {
    statusBar()->clearMessage();
  }
}

/// 仅当变化标签为活动标签时在状态栏显示加载错误。
/// @param id 变化标签标识。
/// @param error_code CEF 错误码。
/// @param failed_url 失败的 UTF-8 地址。
/// @param error_text UTF-8 错误说明。
void TabbedBrowserWindow::OnTabLoadError(
    offscreen::TabManager::TabId id, int error_code,
    const std::string& failed_url, const std::string& error_text) {
  (void)error_code;
  auto* active_bw = qobject_cast<offscreen::BrowserWidget*>(
      tab_widget_->currentWidget());
  if (!active_bw || active_bw->tab_id() != id) {
    return;
  }
  statusBar()->showMessage(QStringLiteral("Failed to load %1: %2")
                               .arg(StringToQString(failed_url),
                                    StringToQString(error_text)));
}

/// 仅当下载所属标签为活动标签时在状态栏显示下载进展。
/// @param id 下载所属标签。
/// @param state 下载状态：0 开始、1 完成、2 取消。
/// @param file_name UTF-8 文件名。
/// @param full_path UTF-8 完整保存路径。
void TabbedBrowserWindow::OnTabDownloadStateChanged(
    offscreen::TabManager::TabId id, int state,
    const std::string& file_name, const std::string& full_path) {
  auto* active_bw = qobject_cast<offscreen::BrowserWidget*>(
      tab_widget_->currentWidget());
  if (!active_bw || active_bw->tab_id() != id) {
    return;
  }
  switch (state) {
    case 0:
      statusBar()->showMessage(QStringLiteral("Download started: %1")
                                   .arg(StringToQString(file_name)));
      break;
    case 1:
      statusBar()->showMessage(QStringLiteral("Download complete: %1")
                                   .arg(StringToQString(full_path)));
      break;
    case 2:
      statusBar()->showMessage(QStringLiteral("Download cancelled: %1")
                                   .arg(StringToQString(file_name)));
      break;
    default:
      break;
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
