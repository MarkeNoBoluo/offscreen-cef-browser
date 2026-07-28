#pragma once

#include <vector>
#include <string>

#include <windows.h>
#include <imm.h>

#include "include/internal/cef_types_wrappers.h"

namespace offscreen {

/// 管理 IMM32 预编辑状态、候选窗位置，并将其转换为 CEF 所需数据。
class BrowserImeHandler {
 public:
  /// 为指定原生窗口创建输入法处理器。
  /// @param hwnd 接收输入法消息的 Qt 原生窗口句柄。
  explicit BrowserImeHandler(HWND hwnd);
  /// 释放系统插入符与输入法窗口资源。
  ~BrowserImeHandler();

  BrowserImeHandler(const BrowserImeHandler&) = delete;
  BrowserImeHandler& operator=(const BrowserImeHandler&) = delete;

  /// 更新关联的原生窗口。
  /// @param hwnd 新的窗口句柄。
  void SetWindowHandle(HWND hwnd);
  /// 调整 WM_IME_SETCONTEXT 标志，抑制系统组合窗。
  /// @param lParam 可写的 Win32 消息参数。
  void HandleImeSetContext(LPARAM& lParam);
  /// 处理组合文本开始消息并重置局部状态。
  void HandleImeStartComposition();
  /// 提取 WM_IME_COMPOSITION 内容。
  /// @param lParam Win32 组合消息标志。
  /// @param commit_text 输出已提交文本。
  /// @param composition_text 输出预编辑文本。
  /// @param underlines 输出 CEF 分句下划线。
  /// @param selection_range 输出预编辑选择范围。
  /// @return 是否存在需要转发给 CEF 的文本变化。
  bool HandleImeComposition(LPARAM lParam,
                            std::wstring& commit_text,
                            std::wstring& composition_text,
                            std::vector<CefCompositionUnderline>& underlines,
                            CefRange& selection_range);
  /// 结束组合状态并清理插入符。
  void HandleImeEndComposition();
  /// 在键盘布局切换后刷新输入语言记录。
  void HandleInputLanguageChange();
  /// 用 CEF 返回的字符矩形更新候选窗定位信息。
  /// @param selection_range 当前组合选择范围。
  /// @param character_bounds 每个字符的 CEF 像素矩形。
  void UpdateCompositionRange(const CefRange& selection_range,
                              const std::vector<CefRect>& character_bounds);
  /// 将组合文本内索引映射为候选窗插入符位置。
  /// @param composition_index 当前预编辑文本中的光标索引。
  void UpdateCaretPosition(int composition_index);
  /// 清空内部组合状态，不向 CEF 发送取消请求。
  void ResetComposition();
  /// 取消当前组合并隐藏系统插入符。
  void CancelComposition();
  /// 查询是否正在处理预编辑文本。
  /// @return 正在组合时为 true。
  bool is_composing() const { return is_composing_; }

 private:
  /// 创建供 IMM32 定位候选窗使用的系统插入符。
  void CreateImeWindow();
  /// 销毁系统插入符。
  void DestroyImeWindow();
  /// 将系统候选窗和插入符移动至 CEF 返回的位置。
  void MoveImeWindow();
  /// 获取关联窗口的 IMM32 上下文。
  /// @return 成功时返回上下文，否则为 nullptr。
  HIMC GetContext();
  /// 归还由 GetContext 获取的 IMM32 上下文。
  /// @param context 待归还的输入法上下文。
  void ReleaseContext(HIMC context);

  HWND hwnd_;
  bool is_composing_ = false;
  bool system_caret_ = false;
  CefRect caret_rect_;
  LANGID input_language_id_ = 0;
};

}  // namespace offscreen
