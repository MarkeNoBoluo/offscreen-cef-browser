#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>
#include <imm.h>

namespace offscreen {

/// 输入法预编辑文本中一个分句的范围与转换状态。
struct ImeClauseInfo {
  int start = 0;
  int end = 0;
  bool is_converted = false;
};

/// 从 IMM32 提取的完整预编辑文本状态。
struct ImeCompositionData {
  std::wstring text;
  std::vector<ImeClauseInfo> clauses;
  int cursor_position = 0;
  int selection_start = 0;
  int selection_end = 0;
};

/// 从 IMM32 提取的已提交文本。
struct ImeResultData {
  std::wstring text;
};

/// 判断 IME 消息是否携带已提交文本。
/// @param lparam WM_IME_COMPOSITION 的 lParam 标志。
/// @return 存在结果字符串时为 true。
bool ImeHasResultString(LPARAM lparam);
/// 从输入法上下文读取已提交文本。
/// @param context 当前 IMM32 上下文。
/// @param lparam WM_IME_COMPOSITION 的 lParam 标志。
/// @return 已提交的宽字符文本。
ImeResultData ImeExtractResult(HIMC context, LPARAM lparam);
/// 判断 IME 消息是否携带预编辑文本。
/// @param lparam WM_IME_COMPOSITION 的 lParam 标志。
/// @return 存在预编辑字符串时为 true。
bool ImeHasCompositionString(LPARAM lparam);
/// 从输入法上下文读取预编辑文本、分句和光标位置。
/// @param context 当前 IMM32 上下文。
/// @param lparam WM_IME_COMPOSITION 的 lParam 标志。
/// @return 供 CEF 组合文本 API 使用的数据。
ImeCompositionData ImeExtractComposition(HIMC context, LPARAM lparam);
/// 查询当前输入法是否处于预编辑状态。
/// @param context 当前 IMM32 上下文。
/// @return 正在预编辑时为 true。
bool ImeIsCompositionActive(HIMC context);
/// 获取当前键盘布局的语言标识。
/// @return 当前输入语言 LANGID。
LANGID ImeGetInputLanguage();

}  // namespace offscreen
