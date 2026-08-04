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

// 管理多标签页中 BrowserService、BrowserWidget 与输入法对象的共同生命周期。
// 关闭操作先等待 CEF 的异步回调完成，再销毁对应 Qt 控件。
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
        // 防止同一标签页重复发起关闭请求而错误递增等待计数。
        bool close_requested = false;
    };

    /// 创建标签页生命周期管理器。
    /// @param parent Qt 父对象。
    explicit TabManager(QObject* parent = nullptr);
    /// 销毁管理器及仍由其拥有的标签资源。
    ~TabManager() override;

    /// 创建标签控件并异步创建对应 CEF 浏览器。
    /// @param initial_url 首次加载的 UTF-8 地址。
    /// @return 新分配的标签标识。
    TabId CreateTab(const std::string& initial_url);
    /// 请求关闭指定标签页。
    /// @param id 待关闭的标签标识。
    void RequestCloseTab(TabId id);
    /// 请求关闭全部标签页，并在全部 CEF 回调完成后发射 AllTabsClosed。
    void ShutdownAll();
    /// 标记当前活动标签，用于 IME 和工具栏路由。
    /// @param id 新活动标签标识。
    void SetActiveTab(TabId id);

    /// 获取活动标签的浏览器服务。
    /// @return 服务指针；无活动标签时为 nullptr。
    BrowserService* ActiveBrowserService() const;
    /// 获取活动标签的 Qt 控件。
    /// @return 控件指针；无活动标签时为 nullptr。
    BrowserWidget* ActiveBrowserWidget() const;
    /// 获取活动标签标识。
    /// @return 活动标签标识。
    TabId ActiveTabId() const;
    /// 根据控件查找其标签标识。
    /// @param widget 要查询的浏览器控件。
    /// @return 匹配的标识；不存在时为 kInvalidTabId。
    TabId TabIdForWidget(BrowserWidget* widget) const;
    /// 根据控件查找其浏览器服务。
    /// @param widget 要查询的浏览器控件。
    /// @return 服务指针；控件无对应标签时为 nullptr。
    BrowserService* BrowserServiceForWidget(BrowserWidget* widget) const;
    /// 判断所有标签是否已移除。
    /// @return 没有活动标签时为 true。
    bool all_closed() const;
    /// 获取当前标签数量。
    /// @return 标签总数。
    int tab_count() const;

    /// 选择性拦截 Win32 输入法消息并转发给活动标签。
    /// @param eventType Qt 提供的原生事件类型。
    /// @param message Win32 MSG 指针。
    /// @param result 输出处理结果。
    /// @return 消息被浏览器处理时为 true。
    bool nativeEventFilter(const QByteArray& eventType, void* message,
                           qintptr* result) override;

signals:
    /// Qt 控件创建并插入标签映射后发射。
    /// @param id 新标签标识。
    /// @param widget 新浏览器控件。
    /// @param title 初始标题。
    void TabCreated(TabId id, BrowserWidget* widget, const std::string& title);
    /// 标签清理前发射，供宿主移除 Qt 标签页。
    /// @param id 已关闭标签标识。
    void TabClosed(TabId id);
    /// 标签主框架地址变化时发射。
    /// @param id 变化标签标识。
    /// @param url 最新 UTF-8 地址。
    void TabAddressChanged(TabId id, const std::string& url);
    /// 标签标题变化时发射。
    /// @param id 变化标签标识。
    /// @param title 最新 UTF-8 标题。
    void TabTitleChanged(TabId id, const std::string& title);
    /// 标签加载状态变化时发射。
    /// @param id 变化标签标识。
    /// @param is_loading 是否正在加载。
    /// @param can_go_back 是否可后退。
    /// @param can_go_forward 是否可前进。
    void TabLoadStateChanged(TabId id, bool is_loading, bool can_go_back,
                             bool can_go_forward);
    /// 标签主框架加载错误时发射。
    /// @param id 变化标签标识。
    /// @param error_code CEF 错误码。
    /// @param failed_url 失败的 UTF-8 地址。
    /// @param error_text UTF-8 错误说明。
    void TabLoadError(TabId id, int error_code,
                      const std::string& failed_url,
                      const std::string& error_text);
    /// 标签下载状态变化时发射。
    /// @param id 下载所属标签。
    /// @param state 下载状态：0 开始、1 完成、2 取消。
    /// @param file_name UTF-8 文件名。
    /// @param full_path UTF-8 完整保存路径。
    void TabDownloadStateChanged(TabId id, int state,
                                 const std::string& file_name,
                                 const std::string& full_path);
    /// 所有 CEF 浏览器关闭且标签资源清理完毕时发射。
    void AllTabsClosed();

private:
    /// 处理 CEF 浏览器完成关闭的回调。
    /// @param id 已关闭浏览器所属标签。
    void OnTabBrowserClosed(TabId id);
    /// 将 CEF 弹出请求转换为新标签。
    /// @param source_id 请求来源标签。
    /// @param url 弹出目标的 UTF-8 地址。
    void OnPopupRequest(TabId source_id, const std::string& url);
    /// 转发标签地址变化。
    /// @param id 变化标签标识。
    /// @param url 最新 UTF-8 地址。
    void OnTabAddressChange(TabId id, const std::string& url);
    /// 转发标签标题变化。
    /// @param id 变化标签标识。
    /// @param title 最新 UTF-8 标题。
    void OnTabTitleChange(TabId id, const std::string& title);
    /// 转发标签加载状态变化。
    /// @param id 变化标签标识。
    /// @param is_loading 是否正在加载。
    /// @param can_go_back 是否可后退。
    /// @param can_go_forward 是否可前进。
    void OnTabLoadStateChange(TabId id, bool is_loading, bool can_go_back,
                              bool can_go_forward);
    /// 转发标签加载错误。
    /// @param id 变化标签标识。
    /// @param error_code CEF 错误码。
    /// @param failed_url 失败的 UTF-8 地址。
    /// @param error_text UTF-8 错误说明。
    void OnTabLoadError(TabId id, int error_code,
                        const std::string& failed_url,
                        const std::string& error_text);
    /// 转发标签下载状态变化。
    /// @param id 下载所属标签。
    /// @param state 下载状态。
    /// @param file_name UTF-8 文件名。
    /// @param full_path UTF-8 完整保存路径。
    void OnTabDownloadStateChange(TabId id, int state,
                                  const std::string& file_name,
                                  const std::string& full_path);
    /// 连接 BrowserService 回调与管理器槽逻辑。
    /// @param entry 待连接的标签条目。
    void WireTabCallbacks(TabEntry& entry);
    /// 解绑并延迟销毁标签控件。
    /// @param id 待清理的标签标识。
    void CleanupTab(TabId id);

    std::unordered_map<TabId, std::unique_ptr<TabEntry>> tabs_by_id_;
    std::unordered_map<BrowserWidget*, TabId> tab_id_by_widget_;
    TabId active_tab_id_ = kInvalidTabId;
    TabId next_tab_id_ = 0;
    bool shutting_down_ = false;
    // 已发起、但尚未收到 CEF OnBeforeClose 的标签数量。
    int pending_close_count_ = 0;
};

}  // namespace offscreen
