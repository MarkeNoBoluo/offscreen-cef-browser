#pragma once

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <windows.h>

namespace offscreen {

class BrowserWidget;
class BrowserImeHandler;
class BrowserService;

class TabManager : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    using TabId = int;
    static constexpr TabId kInvalidTabId = -1;

    struct TabEntry {
        TabId id = kInvalidTabId;
        std::unique_ptr<BrowserService> browser_service;
        BrowserWidget* widget = nullptr;
        std::unique_ptr<BrowserImeHandler> ime_handler;
        bool close_requested = false;
    };

    explicit TabManager(QObject* parent = nullptr);
    ~TabManager() override;

    TabId CreateTab(const std::string& initial_url);
    void RequestCloseTab(TabId id);
    void ShutdownAll();
    void SetActiveTab(TabId id);

    BrowserService* ActiveBrowserService() const;
    BrowserWidget* ActiveBrowserWidget() const;
    TabId ActiveTabId() const;
    TabId TabIdForWidget(BrowserWidget* widget) const;
    bool all_closed() const;
    int tab_count() const;

    bool nativeEventFilter(const QByteArray& eventType, void* message,
                           long* result) override;

signals:
    void TabCreated(TabId id, BrowserWidget* widget, const std::string& title);
    void TabClosed(TabId id);
    void TabAddressChanged(TabId id, const std::string& url);
    void TabTitleChanged(TabId id, const std::string& title);
    void AllTabsClosed();

private:
    void OnTabBrowserClosed(TabId id);
    void OnPopupRequest(TabId source_id, const std::string& url);
    void OnTabAddressChange(TabId id, const std::string& url);
    void OnTabTitleChange(TabId id, const std::string& title);
    void WireTabCallbacks(TabEntry& entry);
    void CleanupTab(TabId id);

    std::unordered_map<TabId, std::unique_ptr<TabEntry>> tabs_by_id_;
    std::unordered_map<BrowserWidget*, TabId> tab_id_by_widget_;
    TabId active_tab_id_ = kInvalidTabId;
    TabId next_tab_id_ = 0;
    bool shutting_down_ = false;
    int pending_close_count_ = 0;
};

}  // namespace offscreen
