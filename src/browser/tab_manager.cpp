#include "browser/tab_manager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>

#include <sstream>

#include "app/diagnostic_log.h"
#include "browser/browser_ime_handler.h"
#include "browser/browser_service.h"
#include "qt/browser_widget.h"

namespace offscreen {

namespace {

/// 判断窗口是否为指定父窗口本身或其子窗口。
/// @param parent 候选父窗口。
/// @param window 待检查窗口。
/// @return 两者存在父子关系时为 true。
bool IsSameOrChild(HWND parent, HWND window) {
    return parent && window &&
           (parent == window || ::IsChild(parent, window));
}

/// 判断 Win32 消息是否属于需要路由的输入法消息集合。
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

}  // namespace

TabManager::TabManager(QObject* parent) : QObject(parent) {
    DiagnosticLog("TabManager constructed");
}

TabManager::~TabManager() = default;

void TabManager::WireTabCallbacks(TabEntry& entry) {
    const TabId id = entry.id;
    auto* service = entry.browser_service.get();

    service->SetBrowserClosedCallback([this, id]() {
        OnTabBrowserClosed(id);
    });

    service->SetPopupRequestCallback([this, id](const std::string& url) {
        OnPopupRequest(id, url);
    });

    service->SetAddressChangeCallback([this, id](const std::string& url) {
        OnTabAddressChange(id, url);
    });

    service->SetTitleChangeCallback([this, id](const std::string& title) {
        OnTabTitleChange(id, title);
    });

    service->SetLoadStateChangeCallback(
        [this, id](bool is_loading, bool can_go_back, bool can_go_forward) {
            OnTabLoadStateChange(id, is_loading, can_go_back, can_go_forward);
        });

    service->SetLoadErrorCallback(
        [this, id](int error_code, const std::string& failed_url,
                   const std::string& error_text) {
            OnTabLoadError(id, error_code, failed_url, error_text);
        });

    service->SetDownloadStateChangeCallback(
        [this, id](int state, const std::string& file_name,
                   const std::string& full_path) {
            OnTabDownloadStateChange(id, state, file_name, full_path);
        });

    service->SetContextMenuRequestedCallback(
        [this, id](int view_x, int view_y) {
            auto it = tabs_by_id_.find(id);
            if (it != tabs_by_id_.end() && it->second->widget) {
                it->second->widget->RequestContextMenu(view_x, view_y);
            }
        });
}

TabManager::TabId TabManager::CreateTab(const std::string& initial_url) {
    const TabId id = next_tab_id_++;
    DiagnosticLog("TabManager::CreateTab id=" + std::to_string(id) +
                  " url=[" + initial_url + "]");

    auto entry = std::make_unique<TabEntry>();
    entry->id = id;
    entry->browser_service = std::make_unique<BrowserService>();

    // 下载默认存入系统下载目录，目录不可用时回退到文档目录，再不可用则由 CEF 使用默认目录。
    QString download_dir =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (download_dir.isEmpty()) {
        download_dir =
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    if (!download_dir.isEmpty()) {
        entry->browser_service->SetDownloadDirectory(
            QDir::toNativeSeparators(download_dir).toStdWString());
    }

    WireTabCallbacks(*entry);

    auto* widget = new BrowserWidget();
    widget->SetFrame(entry->browser_service->frame());
    widget->SetRenderStats(entry->browser_service->render_stats());
    widget->SetBrowserService(entry->browser_service.get());
    widget->SetTabId(id);

    entry->widget = widget;
    tab_id_by_widget_[widget] = id;

    tabs_by_id_[id] = std::move(entry);

    emit TabCreated(id, widget, "Loading...");

    // 等 Qt 将控件加入标签页并创建原生窗口后，再以有效 HWND 创建无窗口 CEF 浏览器。
    QTimer::singleShot(0, this, [this, id, initial_url]() {
        auto it = tabs_by_id_.find(id);
        if (it == tabs_by_id_.end()) return;
        auto& entry = it->second;
        if (!entry->widget) return;

        entry->ime_handler = std::make_unique<BrowserImeHandler>(
            entry->widget->NativeParentHandle());
        entry->widget->SetImeHandler(entry->ime_handler.get());

        entry->widget->SetResizeCallback(
            [service = entry->browser_service.get()](
                BrowserViewRect view_rect, double device_scale_factor) {
                service->Resize(view_rect, device_scale_factor);
            });

        entry->browser_service->SetPaintUpdateCallback(
            [widget = entry->widget](
                const std::vector<BrowserViewRect>& dirty_rects) {
                widget->ScheduleFrameUpdate(dirty_rects);
            });

        QPointer<BrowserWidget> widget_guard(entry->widget);
        entry->browser_service->SetCursorChangeCallback(
            [widget_guard](int cursor_type, HCURSOR cursor_handle) {
                if (!widget_guard) return;
                // 用队列在控件仍存活时更新鼠标样式，避免回调直接持有已销毁的控件。
                QMetaObject::invokeMethod(
                    widget_guard.data(),
                    [widget_guard, cursor_type, cursor_handle]() {
                        if (widget_guard) {
                            widget_guard->SetCefCursor(cursor_type, cursor_handle);
                        }
                    },
                    Qt::QueuedConnection);
            });

        entry->browser_service->SetImeCompositionRangeChangedCallback(
            [widget = entry->widget](
                const CefRange& selected_range,
                const std::vector<CefRect>& bounds) {
                widget->OnImeCompositionRangeChanged(selected_range, bounds);
            });

        if (!entry->browser_service->CreateBrowser(
                entry->widget->NativeParentHandle(),
                entry->widget->CurrentViewRect(),
                entry->widget->CurrentDeviceScaleFactor(),
                initial_url)) {
            DiagnosticLog("TabManager::CreateTab CreateBrowser failed for id=" +
                          std::to_string(id));
            CleanupTab(id);
        }
    });

    return id;
}

void TabManager::RequestCloseTab(TabId id) {
    auto it = tabs_by_id_.find(id);
    if (it == tabs_by_id_.end()) return;
    auto& entry = it->second;
    if (entry->close_requested) return;

    DiagnosticLog("TabManager::RequestCloseTab id=" + std::to_string(id));

    entry->close_requested = true;
    pending_close_count_++;

    const bool can_close_now = entry->browser_service->TryCloseBrowser();
    if (tabs_by_id_.find(id) == tabs_by_id_.end()) {
        return;
    }
    if (can_close_now) {
        pending_close_count_--;
        CleanupTab(id);
    }
}

void TabManager::ShutdownAll() {
    shutting_down_ = true;
    DiagnosticLog("TabManager::ShutdownAll tab_count=" +
                  std::to_string(static_cast<int>(tabs_by_id_.size())));

    std::vector<TabId> all_ids;
    for (auto& [id, _] : tabs_by_id_) {
        all_ids.push_back(id);
    }
    for (TabId id : all_ids) {
        RequestCloseTab(id);
    }

    if (pending_close_count_ == 0) {
        emit AllTabsClosed();
    }
}

void TabManager::SetActiveTab(TabId id) {
    if (active_tab_id_ == id) return;
    DiagnosticLog("TabManager::SetActiveTab id=" + std::to_string(id) +
                  " prev=" + std::to_string(active_tab_id_));
    active_tab_id_ = id;
}

BrowserService* TabManager::ActiveBrowserService() const {
    auto it = tabs_by_id_.find(active_tab_id_);
    return (it != tabs_by_id_.end()) ? it->second->browser_service.get() : nullptr;
}

BrowserWidget* TabManager::ActiveBrowserWidget() const {
    auto it = tabs_by_id_.find(active_tab_id_);
    return (it != tabs_by_id_.end()) ? it->second->widget : nullptr;
}

TabManager::TabId TabManager::ActiveTabId() const {
    return active_tab_id_;
}

TabManager::TabId TabManager::TabIdForWidget(BrowserWidget* widget) const {
    auto it = tab_id_by_widget_.find(widget);
    return (it != tab_id_by_widget_.end()) ? it->second : kInvalidTabId;
}

BrowserService* TabManager::BrowserServiceForWidget(
    BrowserWidget* widget) const {
    const TabId id = TabIdForWidget(widget);
    if (id == kInvalidTabId) {
        return nullptr;
    }
    auto it = tabs_by_id_.find(id);
    return (it != tabs_by_id_.end()) ? it->second->browser_service.get()
                                     : nullptr;
}

bool TabManager::all_closed() const {
    return tabs_by_id_.empty();
}

int TabManager::tab_count() const {
    return static_cast<int>(tabs_by_id_.size());
}

bool TabManager::nativeEventFilter(const QByteArray& eventType, void* message,
                                   qintptr* result) {
    if (eventType != "windows_generic_MSG" &&
        eventType != "windows_dispatcher_MSG") {
        return false;
    }
    MSG* msg = static_cast<MSG*>(message);
    if (!IsImeMessage(msg->message)) return false;

    BrowserWidget* w = ActiveBrowserWidget();
    const HWND msg_hwnd = msg->hwnd;
    const HWND browser_hwnd =
        w ? reinterpret_cast<HWND>(w->winId()) : nullptr;
    const HWND top_level_hwnd =
        browser_hwnd ? ::GetAncestor(browser_hwnd, GA_ROOT) : nullptr;
    QWidget* qt_focus_widget = QApplication::focusWidget();

    const bool targets_browser = msg_hwnd == browser_hwnd;
    const bool targets_browser_child = IsSameOrChild(browser_hwnd, msg_hwnd);
    const bool targets_browser_ancestor =
        IsSameOrChild(msg_hwnd, browser_hwnd);
    const bool targets_top_level = msg_hwnd == top_level_hwnd;
    const bool qt_focus_in_browser =
        w && qt_focus_widget &&
        (qt_focus_widget == w || w->isAncestorOf(qt_focus_widget));
    const bool route_directly = targets_browser || targets_browser_child;
    const bool route_from_ancestor =
        targets_browser_ancestor && qt_focus_in_browser;

    // IME 消息有时由顶层窗口接收。仅当活动浏览器拥有 Qt 焦点时才从祖先窗口
    // 转发，避免多个标签或其他控件收到同一段预编辑文本。
    std::ostringstream stream;
    stream << "TabManager::nativeEventFilter event_type=["
           << eventType.constData() << "] message="
           << HexValue(static_cast<uintptr_t>(msg->message))
           << " msg_hwnd="
           << HexValue(reinterpret_cast<uintptr_t>(msg_hwnd))
           << " browser_hwnd="
           << HexValue(reinterpret_cast<uintptr_t>(browser_hwnd))
           << " top_level_hwnd="
           << HexValue(reinterpret_cast<uintptr_t>(top_level_hwnd))
           << " qt_focus_widget="
           << HexValue(reinterpret_cast<uintptr_t>(qt_focus_widget))
           << " targets_browser=" << (targets_browser ? "true" : "false")
           << " targets_browser_child="
           << (targets_browser_child ? "true" : "false")
           << " targets_browser_ancestor="
           << (targets_browser_ancestor ? "true" : "false")
           << " targets_top_level="
           << (targets_top_level ? "true" : "false")
           << " qt_focus_in_browser="
           << (qt_focus_in_browser ? "true" : "false");

    if (!w) {
        stream << " route=reject_no_active_browser";
        DiagnosticLog(stream.str());
        return false;
    }
    if (route_directly || route_from_ancestor) {
        stream << " route="
               << (route_directly ? "browser_or_child" : "browser_ancestor");
        DiagnosticLog(stream.str());
        return w->HandleImeNativeMessage(msg, result);
    }

    stream << " route=reject_window_mismatch";
    DiagnosticLog(stream.str());
    return false;
}

void TabManager::OnTabBrowserClosed(TabId id) {
    DiagnosticLog("TabManager::OnTabBrowserClosed id=" + std::to_string(id));
    auto it = tabs_by_id_.find(id);
    if (it == tabs_by_id_.end()) return;
    if (it->second->close_requested) {
        pending_close_count_--;
    }
    // 让 CEF 的关闭回调先完整返回，再释放其仍可能引用的 Qt 控件。
    QTimer::singleShot(0, this, [this, id]() {
        CleanupTab(id);
        if (shutting_down_ && pending_close_count_ == 0) {
            emit AllTabsClosed();
        }
    });
}

void TabManager::OnPopupRequest(TabId source_id, const std::string& url) {
    DiagnosticLog("TabManager::OnPopupRequest source=" + std::to_string(source_id) +
                  " url=[" + url + "]");
    CreateTab(url);
}

void TabManager::OnTabAddressChange(TabId id, const std::string& url) {
    DiagnosticLog("TabManager::OnTabAddressChange id=" + std::to_string(id) +
                  " url=[" + url + "]");
    emit TabAddressChanged(id, url);
}

void TabManager::OnTabTitleChange(TabId id, const std::string& title) {
    DiagnosticLog("TabManager::OnTabTitleChange id=" + std::to_string(id) +
                  " title=[" + title + "]");
    emit TabTitleChanged(id, title);
}

void TabManager::OnTabLoadStateChange(TabId id, bool is_loading,
                                      bool can_go_back, bool can_go_forward) {
    DiagnosticLog("TabManager::OnTabLoadStateChange id=" +
                  std::to_string(id) + " is_loading=" +
                  (is_loading ? "true" : "false") + " back=" +
                  (can_go_back ? "true" : "false") + " forward=" +
                  (can_go_forward ? "true" : "false"));
    emit TabLoadStateChanged(id, is_loading, can_go_back, can_go_forward);
}

void TabManager::OnTabLoadError(TabId id, int error_code,
                                const std::string& failed_url,
                                const std::string& error_text) {
    DiagnosticLog("TabManager::OnTabLoadError id=" + std::to_string(id) +
                  " code=" + std::to_string(error_code) + " url=[" +
                  failed_url + "] error=[" + error_text + "]");
    emit TabLoadError(id, error_code, failed_url, error_text);
}

void TabManager::OnTabDownloadStateChange(TabId id, int state,
                                          const std::string& file_name,
                                          const std::string& full_path) {
    DiagnosticLog("TabManager::OnTabDownloadStateChange id=" +
                  std::to_string(id) + " state=" + std::to_string(state) +
                  " name=[" + file_name + "] path=[" + full_path + "]");
    emit TabDownloadStateChanged(id, state, file_name, full_path);
}

void TabManager::CleanupTab(TabId id) {
    DiagnosticLog("TabManager::CleanupTab id=" + std::to_string(id));
    auto it = tabs_by_id_.find(id);
    if (it == tabs_by_id_.end()) return;
    auto& entry = it->second;
    BrowserWidget* widget = entry->widget;

    emit TabClosed(id);
    if (active_tab_id_ == id) {
        active_tab_id_ = kInvalidTabId;
    }
    tab_id_by_widget_.erase(widget);
    if (widget) {
        widget->SetResizeCallback({});
        widget->SetBrowserService(nullptr);
        widget->SetImeHandler(nullptr);
        widget->SetTabId(kInvalidTabId);
    }
    tabs_by_id_.erase(it);
    if (widget) {
        widget->deleteLater();
    }
}

}  // namespace offscreen
