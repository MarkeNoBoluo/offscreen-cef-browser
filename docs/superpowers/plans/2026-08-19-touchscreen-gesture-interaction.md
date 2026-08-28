# 触摸屏手势交互实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 CEF-SDK 中为 Qt/CEF OSR 浏览器增加可验证的触摸滑动、双指缩放、长按、长按拖拽和左右滑历史导航，并把同一行为移植到 cef-96、cef-98、cef-102、cef-112 分支。

**Architecture:** 在 `offscreen_core` 增加不依赖 Qt/CEF 的触摸手势状态机，统一按 DIP 计算阈值并通过纯逻辑测试覆盖判定和冲突优先级。`BrowserWidget` 负责把 Qt `QTouchEvent` 转成触点快照，`BrowserService` 负责调用 CEF `CefBrowserHost::SendTouchEvent`；左右滑导航使用已有 `GoBack()`/`GoForward()`，长按拖拽复用现有 CEF drag-target 生命周期。CEF 100 先实现，再按每个版本分支实际代码结构逐分支移植和验证，不直接复制不兼容的大文件。

**Tech Stack:** C++17, Qt 6.9.3, CEF 100/96/98/102/112, CMake, CTest, MSVC 2022, Windows x64。

**Spec:** `D:/Git/CEF-SDK/.git/gitee-workflow/gw-20260819-110105-f925df/briefs/acceptance-criteria.md` 及已批准的会话设计。

## Global Constraints

- 所有手势阈值按 Qt/CEF 逻辑坐标 DIP 计算，不按物理渲染像素计算。
- 单指滑动阈值 15 DIP；水平位移大于垂直位移为水平，否则为垂直；小于阈值保留点击。
- 双指距离变化达到 20 DIP 才触发；缩放倍率限制 0.5 到 3.0；单指不得进入缩放状态。
- 长按需要持续 500ms 且位移小于 10 DIP；超出位移或提前抬起取消。
- 长按成功后继续移动至少 12 DIP 才进入拖拽；抬起结束拖拽；未完成长按的移动按普通滑动处理。
- 左右滑导航需要水平位移至少 80 DIP 且垂直位移小于 30 DIP；左向右后退，右向左前进；不得误触发网页垂直滚动。
- 不修改现有鼠标、键盘、IME 和外部文件拖拽行为；触摸输入必须通过独立路径接入。
- CEF 触摸事件顺序必须严格保持每个触点的 PRESSED → MOVED → RELEASED/CANCELLED。
- 仅在系统边界校验 Qt 触点输入和浏览器句柄状态；不添加与需求无关的抽象、兼容层或配置项。
- 每个 CEF 版本分支独立构建和测试；缺少对应 SDK 时记录阻塞，不声称运行时验证通过。

---

### Task 1: 建立纯逻辑触摸手势状态机

**Files:**
- Create: `src/browser/browser_touch_gesture.h`
- Create: `src/browser/browser_touch_gesture.cpp`
- Modify: `src/CMakeLists.txt:10-35`，将新源文件加入 `offscreen_core` 或当前纯逻辑库的源文件列表
- Test: `tests/browser_touch_gesture_tests.cpp`
- Modify: `tests/CMakeLists.txt:12-48`，注册 `browser_touch_gesture_tests`

**Interfaces:**
- Consumes: 触点快照、逻辑时间戳和手势阈值常量；不依赖 Qt、CEF 或窗口对象。
- Produces: `TouchPointSnapshot`、`TouchGestureAction`、`TouchGestureStateMachine`，供 Qt 层调用并可在五个 CEF 分支中按相同接口移植。

- [ ] **Step 1: 定义纯逻辑输入输出类型和状态。**
  - `TouchPointSnapshot` 至少包含 `id`、`x`、`y`、`pressed`。
  - `TouchGestureAction` 覆盖 `kNone`、`kForward`、`kBack`、`kBeginDrag`、`kUpdateDrag`、`kEndDrag`、`kCancel`。
  - 状态至少区分空闲、单指候选、普通滑动、双指缩放、长按候选、长按拖拽和已取消序列。
  - 将阈值作为固定常量或 `TouchGestureThresholds` 值类型：15、20、500、10、12、80、30；倍率上下限为 0.5 和 3.0。

- [ ] **Step 2: 先写边界失败测试。**
  - 单指移动 14 DIP 不产生滑动动作，移动 15 DIP 进入滑动。
  - 水平和垂直方向分别按位移比较判定；相等时按垂直滑动处理。
  - 双指距离变化 19 DIP 不缩放，20 DIP 进入缩放；单指永不缩放。
  - 长按 499ms 不触发，500ms 且位移 9 DIP 触发；位移达到 10 DIP 取消。
  - 长按后移动 11 DIP 不拖拽，12 DIP 产生 `kBeginDrag`，后续移动产生 `kUpdateDrag`，抬起产生 `kEndDrag`。
  - 长按前移动按普通滑动处理，不产生拖拽。
  - 水平 79 DIP 不导航，水平 80 DIP 且垂直 29 DIP 导航；垂直达到 30 DIP 不导航。
  - 左向右产生后退，右向左产生前进；导航动作每个触摸序列最多触发一次。

- [ ] **Step 3: 运行纯逻辑测试确认失败。**
  - Run: `cmake --build build/cef100-msvc2022-x64 --config Release --target browser_touch_gesture_tests`
  - Expected: 新测试尚未通过，记录缺失接口或断言失败到 Phase 3 报告，不修改测试绕过失败。

- [ ] **Step 4: 实现最小状态转换。**
  - 首个触点按下记录起点和最近位置，并启动由调用方提供的逻辑时间基准。
  - 第二个触点有效时切换到缩放候选，清除单指导航/拖拽候选，使用两点距离变化判断缩放。
  - 单指位移达到 15 DIP 后判定方向；只有水平且垂直位移小于 30 DIP、水平位移达到 80 DIP 时发出导航动作，否则保留普通触摸滑动。
  - 长按判断只依赖时间和起点位移；进入拖拽前不发出拖拽动作，达到 12 DIP 后发出开始动作。
  - 释放、取消、触点丢失统一清理序列状态，并发出必要的结束或取消动作。
  - 不在状态机中调用 Qt、CEF、导航或拖拽 API。

- [ ] **Step 5: 运行测试并补齐转换覆盖。**
  - Run: `ctest --test-dir build/cef100-msvc2022-x64 -C Release -R browser_touch_gesture_tests --output-on-failure`
  - Expected: 状态边界和冲突优先级测试通过；若构建目录不存在，使用仓库构建脚本生成后再运行。

### Task 2: 将 Qt 触摸事件接入 BrowserWidget

**Files:**
- Modify: `src/qt/browser_widget.h:114-215`
- Modify: `src/qt/browser_widget.cpp:19-42, 838-927, 1037-1061`
- Modify: `src/CMakeLists.txt:10-35`，确认手势逻辑源文件被正确链接

**Interfaces:**
- Consumes: `QTouchEvent` 的触点 ID、位置、状态和时间；Task 1 的 `TouchGestureStateMachine`。
- Produces: 对 BrowserService 的触摸事件发送、导航调用和拖拽动作转发；保持鼠标事件路径不变。

- [ ] **Step 1: 添加失败级别的 Qt 接入编译检查和事件测试入口。**
  - 在头文件增加 `touchEvent(QTouchEvent*) override`、触点快照转换函数、触摸序列清理函数和手势状态成员。
  - 在 cpp 增加 `QTouchEvent` include，并在构造函数启用 `Qt::WA_AcceptTouchEvents`。
  - 不修改现有 `mousePressEvent`、`mouseMoveEvent` 和 `mouseReleaseEvent` 的业务逻辑。

- [ ] **Step 2: 将 Qt 触点坐标转换为 DIP。**
  - 使用 `QTouchEvent::touchPoints()` 的位置，按控件逻辑坐标传给状态机；不乘 `devicePixelRatioF()`。
  - 每个 `QTouchEvent` 按触点状态生成 CEF 事件，保留 ID 和当前坐标；序列结束时对仍活跃触点发送取消或释放。
  - 触点不完整或 `browser_service_ == nullptr` 时安全取消本次触摸序列，不触碰已有鼠标状态。

- [ ] **Step 3: 连接普通触摸和导航动作。**
  - 普通 PRESSED/MOVED/RELEASED/CANCELLED 通过 `BrowserService::SendTouchEvent` 转发。
  - `kBack` 调用 `browser_service_->GoBack()`，`kForward` 调用 `browser_service_->GoForward()`；导航后取消本次触摸序列并阻止重复导航。
  - Qt 事件完成后显式 `accept()`，避免同一触摸被其他 Qt 控件重复处理。

- [ ] **Step 4: 连接长按拖拽动作。**
  - 长按拖拽开始时复用已有 `SendDragTargetDragEnter`/`SendDragTargetDragOver`/`SendDragTargetDrop`/`SendDragTargetDragLeave` 生命周期。
  - 拖拽坐标始终来自当前触点；抬起发送 drop，取消或焦点丢失发送 leave/cancel。
  - 若 CEF 触摸事件和现有拖拽路径产生重复网页事件，只保留状态机确认后的拖拽路径，并增加回归日志，不引入鼠标伪造。

- [ ] **Step 5: 运行 Qt/CEF 编译验证。**
  - Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Architecture x64 -CefRoot <CEF100_ROOT> -QtPrefix <QT_PREFIX> -BuildDir build\cef100-msvc2022-x64 -Configuration Release`
  - Expected: `offscreen_cef_widgets`、demo 和测试目标编译成功；触摸接入不破坏已有鼠标/IME/拖拽编译。

### Task 3: 增加 BrowserService 的 CEF TouchEvent 适配

**Files:**
- Modify: `src/browser/browser_service.h:202-268`
- Modify: `src/browser/browser_service.cpp:500-610`
- Test: `tests/browser_touch_gesture_tests.cpp` 或独立 service adapter test（仅在不引入 Qt/CEF 测试依赖时）

**Interfaces:**
- Consumes: `BrowserWidget` 传入的触点 ID、DIP 坐标、`CEF_TET_*` 状态、修饰键和触点属性。
- Produces: `void SendTouchEvent(int id, float x, float y, int touch_type, int qt_modifiers, float radius_x, float radius_y, float pressure)`，内部构造 CEF 100 兼容的 `CefTouchEvent` 并调用 `browser_->GetHost()->SendTouchEvent`。

- [ ] **Step 1: 写适配边界测试或编译断言。**
  - 验证 PRESSED/MOVED/RELEASED/CANCELLED 的映射值和 modifier 映射路径与现有 `browser_input_mapping` 一致。
  - 不把 CEF 类型暴露到 `offscreen_core` 公共纯逻辑接口。

- [ ] **Step 2: 实现最小 CEF 适配。**
  - 检查 `browser_` 和 `GetHost()` 后构造 `CefTouchEvent`，设置 `id/x/y/radius_x/radius_y/pressure/type/modifiers/pointer_type`。
  - `pointer_type` 固定为 `CEF_POINTER_TYPE_TOUCH`，半径和压力无有效值时为 0。
  - `touch_type` 仅接受四个已知值；内部调用点来自 Qt 状态机，不额外引入配置或 fallback。

- [ ] **Step 3: 运行构建和现有输入测试。**
  - Run: `ctest --test-dir build/cef100-msvc2022-x64 -C Release -R 'browser_input_mapping_tests|browser_touch_gesture_tests' --output-on-failure`
  - Expected: 现有输入映射和新增触摸状态测试通过。

### Task 4: CEF 100 集成回归和验收页验证

**Files:**
- Modify: `tests/visualization_compatibility_test.html` 或新增最小触摸验收页（仅当已有页面无法验证手势时）
- Modify: `tests/CMakeLists.txt` only if a new test executable is needed
- Create: `artifacts/3-cef100-touch-validation.md`（运行证据，不进入代码提交）

**Interfaces:**
- Consumes: Task 1–3 的实现和 CEF 100 构建产物。
- Produces: CEF 100 五项手势的真实触摸屏验收记录，以及未能在当前环境验证的项目清单。

- [ ] **Step 1: 运行静态/API 测试。**
  - Run: `ctest --test-dir build/cef100-msvc2022-x64 -C Release --output-on-failure`
  - Expected: 所有已注册 CTest 通过。

- [ ] **Step 2: 准备真实触摸验收场景。**
  - 验证网页可滚动区域的单指 15 DIP 边界、双指 20 DIP 缩放边界、0.5/3.0 倍限制、500ms/10 DIP 长按、12 DIP 拖拽、80/30 DIP 左右滑导航。
  - 验证小于阈值仍保留点击、垂直大幅移动不触发历史导航、单点不触发缩放。

- [ ] **Step 3: 保存验证证据。**
  - 记录设备、DPI、CEF 版本、构建命令、CTest 结果和五项手势逐项结果。
  - 没有真实触摸屏时明确记录 `NOT VERIFIED`，不得用鼠标模拟结果替代触摸验收。

### Task 5: 将 CEF 100 实现移植到 cef-96 和 cef-98

**Files:**
- Modify per branch: `src/browser/browser_touch_gesture.{h,cpp}`, `src/qt/browser_widget.{h,cpp}`, `src/browser/browser_service.{h,cpp}`, `src/CMakeLists.txt`, `tests/CMakeLists.txt`, tests
- Create: `artifacts/3-port-cef96.md`, `artifacts/3-port-cef98.md`

**Interfaces:**
- Consumes: CEF 100 已通过的纯逻辑状态机接口和 Qt/CEF 接入行为。
- Produces: 每个分支独立的触摸实现提交、构建结果和 CTest 结果。

- [ ] **Step 1: 从每个分支最新远程状态创建对应 issue 分支。**
  - 不覆盖已有分支或工作树；先确认 `gitee/cef-96`、`gitee/cef-98` 当前 SHA。
  - 使用独立 worktree 或串行切换，避免与 cef-100 工作树并行修改。

- [ ] **Step 2: 按分支结构移植纯逻辑模块和 CMake/test 注册。**
  - 保持 Task 1 的公共接口和阈值不变；只调整该分支已有 target/source 布局。

- [ ] **Step 3: 按分支实际 BrowserWidget/BrowserService 结构移植 Qt/CEF 接入。**
  - cef-96/98 的 CEF `SendTouchEvent` 接口和 `CefTouchEvent` 结构须在对应 SDK 中重新确认。
  - 不复制 cef-100 的 GPU、IME 或其他无关变更。

- [ ] **Step 4: 在每个分支运行验证。**
  - Run: 对应分支构建脚本、`ctest --test-dir <build-dir> -C Release -R 'browser_touch_gesture_tests|browser_input_mapping_tests' --output-on-failure`。
  - 保存 commit SHA、构建结果、CTest 结果和未验证原因。

### Task 6: 将 CEF 100 实现移植到 cef-102 和 cef-112

**Files:**
- Modify per branch: 与 Task 5 相同，按分支实际文件结构调整
- Create: `artifacts/3-port-cef102.md`, `artifacts/3-port-cef112.md`

**Interfaces:**
- Consumes: CEF 100 已验证行为和 Task 5 的分支适配经验。
- Produces: cef-102、cef-112 的独立移植提交及验证记录。

- [ ] **Step 1: 获取远程分支并确认基线。**
  - `cef-102` 当前只确认存在于 `gitee/cef-102`，本地没有对应分支；先 fetch 后以远程最新 SHA 创建临时工作树。
  - 继续保持各版本分支独立，不把 cef-100 的 GPU 或构建脚本变更带入。

- [ ] **Step 2: 处理 Qt/CEF 结构差异。**
  - cef-112 的 `BrowserWidget` 继承和 IME/drag 结构与 cef-100 不同，先按接口定位 touchEvent、BrowserService 和现有 drag 生命周期，再手工接入。
  - 重新确认对应 CEF SDK 的 `SendTouchEvent` 与 `CefTouchEvent` 字段。

- [ ] **Step 3: 构建并运行纯逻辑/输入测试。**
  - 运行该分支已有构建脚本和 CTest；至少覆盖触摸状态机边界及现有输入映射回归。
  - SDK、Qt 或 VS 环境缺失时记录 `NOT VERIFIED` 和具体缺失路径。

### Task 7: 汇总移植结果并准备 Phase 4

**Files:**
- Create: `artifacts/3-cross-branch-summary.md`
- Create: `briefs/quality-checklist.md`

**Interfaces:**
- Consumes: cef-100、cef-96、cef-98、cef-102、cef-112 的提交和验证报告。
- Produces: Phase 4 质量审查所需的完整分支矩阵，不修改业务代码。

- [ ] **Step 1: 汇总每个分支的基线、commit SHA、编译结果、CTest 结果和真实触摸验收状态。**
- [ ] **Step 2: 明确未验证项，区分代码缺陷、环境缺失和真实设备尚未验收。**
- [ ] **Step 3: 将完整工作树 diff 和显式文件列表交给 reviewer，等待 Phase 3 的提交确认。**

## 提交与推送顺序

1. 先在 `issue/IK9NXV-cef-sdk-task-3` 的 cef-100 基线完成实现与快速验证。
2. 将 cef-100 的实现提交按文件显式暂存，提交信息使用：
   `feat(touch): add touchscreen gesture interaction close #IK9NXV`
3. 推送到 Gitee 的 Issue 分支（本仓库 Gitee 远程名为 `gitee`，不是 `origin`）：
   `git push -u gitee issue/IK9NXV-cef-sdk-task-3`
4. 每个版本分支从其自身 CEF 基线创建独立 issue 分支或由用户指定的对应分支，完成后分别提交和推送。
5. 所有分支验证结果汇总后再进入 Phase 4；不得将“cef-100 通过”表述为全部版本通过。
