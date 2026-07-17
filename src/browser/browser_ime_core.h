#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>
#include <imm.h>

namespace offscreen {

struct ImeClauseInfo {
  int start = 0;
  int end = 0;
  bool is_converted = false;
};

struct ImeCompositionData {
  std::wstring text;
  std::vector<ImeClauseInfo> clauses;
  int cursor_position = 0;
  int selection_start = 0;
  int selection_end = 0;
};

struct ImeResultData {
  std::wstring text;
};

bool ImeHasResultString(LPARAM lparam);
ImeResultData ImeExtractResult(HIMC context, LPARAM lparam);
bool ImeHasCompositionString(LPARAM lparam);
ImeCompositionData ImeExtractComposition(HIMC context, LPARAM lparam);
bool ImeIsCompositionActive(HIMC context);
LANGID ImeGetInputLanguage();

}  // namespace offscreen
