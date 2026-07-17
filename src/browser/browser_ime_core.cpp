#include "browser/browser_ime_core.h"

#include <cstring>

namespace offscreen {

namespace {

// IMM32 composition attribute values
constexpr uint8_t kAttrInput = 0x00;
constexpr uint8_t kAttrTargetConverted = 0x01;
constexpr uint8_t kAttrConverted = 0x02;
constexpr uint8_t kAttrFixedConverted = 0x03;

}  // namespace

bool ImeHasResultString(LPARAM lparam) {
  return (lparam & GCS_RESULTSTR) != 0;
}

ImeResultData ImeExtractResult(HIMC context, LPARAM lparam) {
  ImeResultData result;
  (void)lparam;

  LONG byte_count = ::ImmGetCompositionStringW(context, GCS_RESULTSTR,
                                               nullptr, 0);
  if (byte_count <= 0) {
    return result;
  }

  std::vector<wchar_t> buf(byte_count / sizeof(wchar_t) + 1, L'\0');
  LONG copied = ::ImmGetCompositionStringW(context, GCS_RESULTSTR,
                                           buf.data(), byte_count);
  if (copied > 0) {
    result.text.assign(buf.data(), static_cast<size_t>(copied / sizeof(wchar_t)));
  }

  return result;
}

bool ImeHasCompositionString(LPARAM lparam) {
  return (lparam & GCS_COMPSTR) != 0;
}

ImeCompositionData ImeExtractComposition(HIMC context, LPARAM lparam) {
  ImeCompositionData data;
  (void)lparam;

  // Extract composition text
  LONG comp_len = ::ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0);
  if (comp_len <= 0) {
    return data;
  }

  std::vector<wchar_t> text_buf(comp_len / sizeof(wchar_t) + 1, L'\0');
  LONG text_copied = ::ImmGetCompositionStringW(context, GCS_COMPSTR,
                                                text_buf.data(), comp_len);
  if (text_copied <= 0) {
    return data;
  }
  data.text.assign(text_buf.data(),
                   static_cast<size_t>(text_copied / sizeof(wchar_t)));
  const int text_length = static_cast<int>(data.text.size());

  // Extract clause boundaries
  LONG clause_len = ::ImmGetCompositionStringW(context, GCS_COMPCLAUSE,
                                               nullptr, 0);
  if (clause_len > 0) {
    std::vector<DWORD> clause_buf(static_cast<size_t>(clause_len) / sizeof(DWORD), 0);
    ::ImmGetCompositionStringW(context, GCS_COMPCLAUSE,
                              clause_buf.data(), clause_len);
    const size_t clause_count = static_cast<size_t>(clause_len) / sizeof(DWORD);

    // clause_buf has n elements; there are n-1 clauses.
    // Clause i spans [buf[i], buf[i+1]).
    for (size_t i = 0; i + 1 < clause_count; ++i) {
      ImeClauseInfo info;
      info.start = static_cast<int>(clause_buf[i]);
      info.end = static_cast<int>(clause_buf[i + 1]);
      data.clauses.push_back(info);
    }
  }

  // If no clauses found, create one spanning the whole text
  if (data.clauses.empty() && text_length > 0) {
    ImeClauseInfo info;
    info.start = 0;
    info.end = text_length;
    data.clauses.push_back(info);
  }

  // Extract per-character attributes to determine converted status
  LONG attr_len = ::ImmGetCompositionStringW(context, GCS_COMPATTR,
                                             nullptr, 0);
  if (attr_len > 0) {
    std::vector<uint8_t> attrs(static_cast<size_t>(attr_len));
    ::ImmGetCompositionStringW(context, GCS_COMPATTR,
                              attrs.data(), attr_len);
    for (auto& clause : data.clauses) {
      if (clause.start < static_cast<int>(attrs.size())) {
        const uint8_t attr = attrs[static_cast<size_t>(clause.start)];
        clause.is_converted = (attr == kAttrConverted ||
                               attr == kAttrFixedConverted);
      }
    }
  }

  // Extract cursor position
  LONG cursor = ::ImmGetCompositionStringW(context, GCS_CURSORPOS,
                                           nullptr, 0);
  data.cursor_position = static_cast<int>(cursor);

  data.selection_start = data.cursor_position;
  data.selection_end = data.cursor_position;

  return data;
}

bool ImeIsCompositionActive(HIMC context) {
  if (!context) {
    return false;
  }
  LONG open_status = ::ImmGetOpenStatus(context);
  return open_status != FALSE;
}

LANGID ImeGetInputLanguage() {
  return LOWORD(::GetKeyboardLayout(0));
}

}  // namespace offscreen
