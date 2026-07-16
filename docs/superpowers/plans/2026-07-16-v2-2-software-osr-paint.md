目标: 将 V2.1 的 no-op MinimalRenderHandler::OnPaint 替换为软件 OSR 显示闭环：CEF BGRA buffer 复制到自有 frame，Qt BrowserWidget 用 QPainter 绘制，并支持 DPI、dirty rect、popup 合成。
推荐路线: 直接新增 BrowserFrame + OsrRenderHandler，替换 MinimalRenderHandler。这是最小正确改动，符合现有 BrowserService/BrowserClient 边界，不引入 OpenGL、输入、IME 或 DevTools UI。
全局约束
- Windows x64 only。
- Qt 5.14.2 MSVC2017 64bit。
- CEF 96 windows64 VS2017 binary distribution。
- CMake-generated Visual Studio 2017 x64 build。
- 软件 OSR only，不实现 QOpenGLWidget、shared texture 或 OnAcceleratedPaint。
- 不实现鼠标、滚轮、键盘、focus、IME、DevTools UI。
- 不主动提交；每个任务末尾只作为提交检查点，执行前需用户确认。
文件结构
- 新增 src/browser/browser_paint_geometry.h/.cpp：Qt-free DPI/dirty rect/popup metadata helper，可进 offscreen_core 单测。
- 新增 tests/browser_paint_geometry_tests.cpp：覆盖物理 dirty rect 到 DIP update rect、scale clamp、popup visibility metadata。
- 新增 src/browser/browser_frame.h/.cpp：线程安全保存 view/popup QImage 与 popup 状态。
- 新增 src/browser/osr_render_handler.h/.cpp：替代 MinimalRenderHandler，实现 GetViewRect、GetScreenInfo、OnPaint、OnPopupShow、OnPopupSize。
- 修改 src/qt/browser_widget.h/.cpp：持有 frame，提供 scale 查询、queued update、paintEvent 绘制。
- 修改 src/browser/browser_client.h/.cpp：render handler 类型从 MinimalRenderHandler 改为 OsrRenderHandler 或通用 CefRenderHandler。
- 修改 src/browser/browser_service.h/.cpp：创建并维护 BrowserFrame/OsrRenderHandler，resize 同步 scale。
- 修改 src/app/main.cpp：把 widget 的 frame/update callback/scale 接到 service。
- 修改 CMakeLists.txt、src/CMakeLists.txt：加入新源文件和新测试，移除或停用 minimal_render_handler.*。
Task 1: Qt-Free Paint Geometry Helpers
Files: create src/browser/browser_paint_geometry.h, src/browser/browser_paint_geometry.cpp, tests/browser_paint_geometry_tests.cpp; modify root CMakeLists.txt。
Interfaces:
struct BrowserPhysicalRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

double NormalizeDeviceScaleFactor(double scale);
BrowserViewRect PhysicalRectToDipUpdateRect(BrowserPhysicalRect rect,
                                             double scale);
Steps:
1. 写失败测试：scale 1.0、1.25、1.5、2.0 的 dirty rect 转 DIP 时向外扩展，不漏像素。
2. 写失败测试：0、负数、非有限 scale clamp 到 1.0。
3. 写失败测试：popup metadata show/hide 不影响 view rect。
4. 实现 helper，使用 floor(x / scale) 和 ceil((x + width) / scale)。
5. 运行：
cmake --build "build\v1-cef96-msvc2017-x64" --config Debug --target offscreen_core_tests
6. 运行：
ctest --test-dir "build\v1-cef96-msvc2017-x64" -C Debug --output-on-failure
Task 2: BrowserFrame Storage
Files: create src/browser/browser_frame.h, src/browser/browser_frame.cpp; modify src/CMakeLists.txt。
Interfaces:
struct BrowserFrameSnapshot {
  bool has_view = false;
  bool popup_visible = false;
  BrowserViewRect popup_rect;
  QImage view_image;
  QImage popup_image;
};

class BrowserFrame {
 public:
  void SetViewImage(const void* bgra_buffer, int width, int height, double scale);
  void SetPopupImage(const void* bgra_buffer, int width, int height, double scale);
  void SetPopupVisible(bool visible);
  void SetPopupRect(BrowserViewRect rect);
  BrowserFrameSnapshot Snapshot() const;
};
Steps:
1. 实现 QImage::Format_ARGB32 拷贝，必须 .copy()，不得保存 CEF buffer 指针。
2. 对合法 image 设置 setDevicePixelRatio(NormalizeDeviceScaleFactor(scale))。
3. null buffer 或非正尺寸直接忽略，保留上一帧。
4. 用 std::mutex 保护 frame 状态；Snapshot() 返回可独立绘制的拷贝。
5. 编译 offscreen_cef_browser 验证 Qt/CEF target 可用。
Task 3: OsrRenderHandler
Files: create src/browser/osr_render_handler.h, src/browser/osr_render_handler.cpp; update src/browser/browser_client.*。
Interfaces:
using PaintUpdateCallback =
    std::function<void(const std::vector<BrowserViewRect>&)>;

class OsrRenderHandler final : public CefRenderHandler {
 public:
  OsrRenderHandler(BrowserViewRect view_rect,
                   double device_scale_factor,
                   std::shared_ptr<BrowserFrame> frame,
                   PaintUpdateCallback paint_update_callback);

  void SetViewRect(BrowserViewRect view_rect, double device_scale_factor);
  BrowserViewRect view_rect() const;

  void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
  bool GetScreenInfo(CefRefPtr<CefBrowser> browser,
                     CefScreenInfo& screen_info) override;
  void OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) override;
  void OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect& rect) override;
  void OnPaint(CefRefPtr<CefBrowser> browser,
               PaintElementType type,
               const RectList& dirtyRects,
               const void* buffer,
               int width,
               int height) override;
};
Steps:
1. GetViewRect 继续返回 DIP view rect。
2. GetScreenInfo 设置 device_scale_factor，rect 和 available_rect 同 view rect。
3. PET_VIEW 时复制到 BrowserFrame::SetViewImage，dirty rect 转 DIP 后触发 update callback。
4. PET_POPUP 时复制到 SetPopupImage，update 区域为 popup rect 或 full update。
5. OnPopupShow(false) 只隐藏 popup，不清空 view；触发 full update 清理旧 popup。
6. 未知 paint type 忽略，不 crash。
7. 更新 BrowserClient 构造参数和成员类型，避免依赖 MinimalRenderHandler。
Task 4: BrowserWidget Paint And Queued Update
Files: modify src/qt/browser_widget.h, src/qt/browser_widget.cpp。
Interfaces:
void SetFrame(std::shared_ptr<BrowserFrame> frame);
double CurrentDeviceScaleFactor() const;
void ScheduleFrameUpdate(const std::vector<BrowserViewRect>& dirty_rects);
Steps:
1. 新增 paintEvent(QPaintEvent*)。
2. 无 frame 或无 view 时填充 neutral background。
3. 先 drawImage(QPoint(0, 0), snapshot.view_image)。
4. popup_visible 且 popup image 非空时，按 popup_rect.x/y 叠加绘制。
5. ScheduleFrameUpdate 用 QMetaObject::invokeMethod(..., Qt::QueuedConnection, ...) 回 GUI 线程。
6. dirty rects 非空时构造 QRegion 调 update(region)，为空时 update()。
7. CurrentDeviceScaleFactor() 优先读 windowHandle()->screen()->devicePixelRatio()，否则 fallback devicePixelRatioF()，非法值按 helper clamp。
Task 5: BrowserService/Main Wiring
Files: modify src/browser/browser_service.h, src/browser/browser_service.cpp, src/app/main.cpp。
Interfaces:
bool CreateBrowser(HWND parent_handle,
                   BrowserViewRect initial_view_rect,
                   double initial_device_scale_factor,
                   const std::string& initial_url);

void Resize(BrowserViewRect view_rect, double device_scale_factor);
std::shared_ptr<BrowserFrame> frame() const;
void SetPaintUpdateCallback(PaintUpdateCallback callback);
Steps:
1. BrowserService 构造或 CreateBrowser 前创建 std::shared_ptr<BrowserFrame>。
2. CreateBrowser 使用 new OsrRenderHandler(...)，不再创建 MinimalRenderHandler。
3. Resize 同时更新 view rect 和 scale，再调用 WasResized()。
4. OnBrowserClosed 清空 browser_、client_、render_handler_，保留或释放 frame 均可，但不要影响关闭流程。
5. main.cpp 创建 widget 后调用 browser_widget_ptr->SetFrame(browser_service.frame())。
6. resize callback 改为传 CurrentViewRect() 和 CurrentDeviceScaleFactor()。
7. paint update callback 调 browser_widget_ptr->ScheduleFrameUpdate(...)。
Task 6: CMake Cleanup And Build
Files: modify root CMakeLists.txt, src/CMakeLists.txt。
Steps:
1. offscreen_core 加入 browser_paint_geometry.*。
2. 新增 browser_paint_geometry_tests 并挂到 offscreen_core_tests 和 ctest。
3. OFFSCREEN_APP_SRCS 加入 browser_frame.*、osr_render_handler.*。
4. 从 app sources 移除 minimal_render_handler.*。
5. 如没有外部引用，删除 minimal_render_handler.* 可作为执行阶段的单独确认点。
6. 构建：
cmake --build "build\v1-cef96-msvc2017-x64" --config Debug --target offscreen_core_tests offscreen_cef_browser offscreen_cef_subprocess
7. 测试：
ctest --test-dir "build\v1-cef96-msvc2017-x64" -C Debug --output-on-failure
Task 7: Runtime Validation
Files: no planned code changes。
Steps:
1. 运行：
build\v1-cef96-msvc2017-x64\src\Debug\offscreen_cef_browser.exe --url=https://example.com/
2. 验证页面可见，不再是空白窗口。
3. resize 窗口，确认画面尺寸更新，无明显拉伸或 stale border。
4. 打开含动画或 loading 的页面，确认后续 repaint 生效。
5. 用本地 HTML select/dropdown 页面手测 popup 合成。
6. 关闭 app 后确认无 offscreen_cef_subprocess.exe 残留。
覆盖检查
- view/popup frame storage：Task 2。
- OnPaint(PET_VIEW/PET_POPUP) copy：Task 3。
- BrowserWidget::paintEvent 绘制：Task 4。
- popup composition：Task 2、3、4。
- dirty rect 物理像素到 DIP：Task 1、3、4。
- device scale factor 和 QImage::devicePixelRatio：Task 1、2、3、4、5。
- 保留 V2.1 lifecycle/build/tests/smoke：Task 5、6、7。
- 明确排除输入、IME、DevTools UI、OpenGL/shared texture：全局约束。