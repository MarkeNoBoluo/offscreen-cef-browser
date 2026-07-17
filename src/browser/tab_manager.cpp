#include "browser/tab_manager.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include "app/diagnostic_log.h"
#include "browser/browser_ime_handler.h"
#include "browser/browser_service.h"
#include "qt/browser_widget.h"

namespace offscreen {

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
}

TabManager::TabId TabManager::CreateTab(const std::string& initial_url) {
    const TabId id = next_tab_id_++;
    DiagnosticLog("TabManager::CreateTab id=" + std::to_string(id) +
                  " url=[" + initial_url + "]");

    auto entry = std::make_unique<TabEntry>();
    entry->id = id;
    entry->browser_service = std::make_unique<BrowserService>();

    WireTabCallbacks(*entry);

    auto* widget = new BrowserWidget();
    widget->SetFrame(entry->browser_service->frame());
    widget->SetBrowserService(entry->browser_service.get());
    widget->SetTabId(id);

    entry->widget = widget;
    tab_id_by_widget_[widget] = id;

    tabs_by_id_[id] = std::move(entry);

    emit TabCreated(id, widget, "Loading...");

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

bool TabManager::all_closed() const {
    return tabs_by_id_.empty();
}

int TabManager::tab_count() const {
    return static_cast<int>(tabs_by_id_.size());
}

bool TabManager::nativeEventFilter(const QByteArray& eventType, void* message,
                                   long* result) {
    if (eventType != "windows_generic_MSG") return false;
    MSG* msg = static_cast<MSG*>(message);

    switch (msg->message) {
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
            break;
        default:
            return false;
    }

    BrowserWidget* w = ActiveBrowserWidget();
    if (!w) return false;

    const HWND msg_hwnd = msg->hwnd;
    const HWND widget_hwnd = reinterpret_cast<HWND>(w->winId());
    if (msg_hwnd == widget_hwnd || ::IsChild(widget_hwnd, msg_hwnd)) {
        return w->HandleImeNativeMessage(msg, result);
    }
    return false;
}

void TabManager::OnTabBrowserClosed(TabId id) {
    DiagnosticLog("TabManager::OnTabBrowserClosed id=" + std::to_string(id));
    auto it = tabs_by_id_.find(id);
    if (it == tabs_by_id_.end()) return;
    if (it->second->close_requested) {
        pending_close_count_--;
    }
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
