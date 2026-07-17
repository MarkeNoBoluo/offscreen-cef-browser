#include "browser/browser_ime_handler.h"

#include <algorithm>
#include <cstring>

#include "app/diagnostic_log.h"
#include "browser/browser_ime_core.h"

namespace offscreen {

namespace {

// Underline colors
constexpr uint32_t kColorBlack = 0xFF000000;
constexpr uint32_t kColorTransparent = 0x00000000;

}  // namespace

BrowserImeHandler::BrowserImeHandler(HWND hwnd)
    : hwnd_(hwnd), input_language_id_(ImeGetInputLanguage()) {
  DiagnosticLog("BrowserImeHandler constructed hwnd=" +
                HexValue(reinterpret_cast<uintptr_t>(hwnd_)) +
                " langid=" + std::to_string(input_language_id_));
}

BrowserImeHandler::~BrowserImeHandler() {
  DiagnosticLog("BrowserImeHandler destructed");
  DestroyImeWindow();
}

void BrowserImeHandler::SetWindowHandle(HWND hwnd) {
  if (!hwnd || hwnd == hwnd_) {
    return;
  }

  DiagnosticLog("BrowserImeHandler::SetWindowHandle old=" +
                HexValue(reinterpret_cast<uintptr_t>(hwnd_)) + " new=" +
                HexValue(reinterpret_cast<uintptr_t>(hwnd)));
  DestroyImeWindow();
  hwnd_ = hwnd;
}

// ImeSetContext - suppress default composition window, let DefWindowProc
// handle the rest (the caller in BrowserWidget returns false/break to
// let Qt/WndProc chain continue).
void BrowserImeHandler::HandleImeSetContext(LPARAM& lParam) {
  const LPARAM before = lParam;
  lParam &= ~ISC_SHOWUICOMPOSITIONWINDOW;
  DiagnosticLog("BrowserImeHandler::HandleImeSetContext before=" +
                HexValue(static_cast<uintptr_t>(before)) + " after=" +
                HexValue(static_cast<uintptr_t>(lParam)));
}

void BrowserImeHandler::HandleImeStartComposition() {
  DiagnosticLog("BrowserImeHandler::HandleImeStartComposition begin");
  DestroyImeWindow();
  is_composing_ = true;
  CreateImeWindow();
  DiagnosticLog("BrowserImeHandler::HandleImeStartComposition end composing=" +
                std::string(is_composing_ ? "true" : "false"));
}

bool BrowserImeHandler::HandleImeComposition(
    LPARAM lParam,
    std::wstring& commit_text,
    std::wstring& composition_text,
    std::vector<CefCompositionUnderline>& underlines,
    CefRange& selection_range) {
  commit_text.clear();
  composition_text.clear();
  underlines.clear();

  HIMC context = GetContext();
  if (!context) {
    DiagnosticLog("BrowserImeHandler::HandleImeComposition no HIMC lParam=" +
                  HexValue(static_cast<uintptr_t>(lParam)));
    return false;
  }

  bool handled = false;
  DiagnosticLog("BrowserImeHandler::HandleImeComposition lParam=" +
                HexValue(static_cast<uintptr_t>(lParam)) +
                " has_result=" +
                (ImeHasResultString(lParam) ? std::string("true")
                                            : std::string("false")) +
                " has_comp=" +
                (ImeHasCompositionString(lParam) ? std::string("true")
                                                 : std::string("false")));

  // Check for result string (committed text)
  if (ImeHasResultString(lParam)) {
    ImeResultData result = ImeExtractResult(context, lParam);
    commit_text = std::move(result.text);
    DiagnosticLog("BrowserImeHandler::HandleImeComposition result_len=" +
                  std::to_string(commit_text.size()));
    handled = true;
  }

  // Check for composition string (in-progress text)
  if (ImeHasCompositionString(lParam)) {
    ImeCompositionData comp = ImeExtractComposition(context, lParam);
    composition_text = std::move(comp.text);
    const int cursor_position =
        std::max(0, std::min(comp.cursor_position,
                             static_cast<int>(composition_text.size())));

    // Build CefCompositionUnderline for each clause
    for (const auto& clause : comp.clauses) {
      CefCompositionUnderline underline;
      underline.range.from = static_cast<uint32_t>(clause.start);
      underline.range.to = static_cast<uint32_t>(clause.end);
      if (clause.is_converted) {
        underline.color = kColorTransparent;
        underline.background_color = kColorTransparent;
        underline.thick = false;
      } else {
        underline.color = kColorBlack;
        underline.background_color = kColorTransparent;
        underline.thick = true;
      }
      underlines.push_back(underline);
    }

    // Build selection range from cursor position
    selection_range.from = static_cast<uint32_t>(cursor_position);
    selection_range.to = static_cast<uint32_t>(cursor_position);

    // Update caret position for candidate window
    if (!comp.clauses.empty()) {
      UpdateCaretPosition(cursor_position);
    }

    DiagnosticLog("BrowserImeHandler::HandleImeComposition comp_len=" +
                  std::to_string(composition_text.size()) +
                  " clauses=" + std::to_string(comp.clauses.size()) +
                  " underlines=" + std::to_string(underlines.size()) +
                  " cursor=" + std::to_string(cursor_position));
    handled = true;
  }

  ReleaseContext(context);
  DiagnosticLog("BrowserImeHandler::HandleImeComposition handled=" +
                std::string(handled ? "true" : "false"));
  return handled;
}

void BrowserImeHandler::HandleImeEndComposition() {
  DiagnosticLog("BrowserImeHandler::HandleImeEndComposition");
  is_composing_ = false;
  DestroyImeWindow();
}

void BrowserImeHandler::HandleInputLanguageChange() {
  input_language_id_ = ImeGetInputLanguage();
  DiagnosticLog("BrowserImeHandler::HandleInputLanguageChange langid=" +
                std::to_string(input_language_id_));
}

void BrowserImeHandler::UpdateCompositionRange(
    const CefRange& selection_range,
    const std::vector<CefRect>& character_bounds) {
  if (!is_composing_) {
    DiagnosticLog("BrowserImeHandler::UpdateCompositionRange ignored "
                  "not composing bounds=" +
                  std::to_string(character_bounds.size()));
    return;
  }

  // Use the character bounds at the selection start to position the caret
  const uint32_t index = selection_range.from;
  if (index < character_bounds.size()) {
    caret_rect_ = character_bounds[index];
  } else if (!character_bounds.empty()) {
    caret_rect_ = character_bounds.back();
  }

  DiagnosticLog("BrowserImeHandler::UpdateCompositionRange selected=" +
                std::to_string(selection_range.from) + "-" +
                std::to_string(selection_range.to) + " bounds=" +
                std::to_string(character_bounds.size()) + " caret=" +
                std::to_string(caret_rect_.x) + "," +
                std::to_string(caret_rect_.y) + " " +
                std::to_string(caret_rect_.width) + "x" +
                std::to_string(caret_rect_.height));
  MoveImeWindow();
}

void BrowserImeHandler::UpdateCaretPosition(int composition_index) {
  // The caret position within the composition text is tracked
  // and MoveImeWindow will use the current caret_rect_
  MoveImeWindow();
  (void)composition_index;
}

void BrowserImeHandler::ResetComposition() {
  DiagnosticLog("BrowserImeHandler::ResetComposition");
  is_composing_ = false;
  DestroyImeWindow();
}

void BrowserImeHandler::CancelComposition() {
  DiagnosticLog("BrowserImeHandler::CancelComposition");
  is_composing_ = false;
  DestroyImeWindow();
}

// --- Private helpers ---

HIMC BrowserImeHandler::GetContext() {
  return ::ImmGetContext(hwnd_);
}

void BrowserImeHandler::ReleaseContext(HIMC context) {
  if (context) {
    ::ImmReleaseContext(hwnd_, context);
  }
}

void BrowserImeHandler::CreateImeWindow() {
  if (!system_caret_) {
    const int caret_w = std::max(caret_rect_.width, 1);
    const int caret_h = std::max(caret_rect_.height, 1);
    ::CreateCaret(hwnd_, nullptr, caret_w, caret_h);
    system_caret_ = true;
    DiagnosticLog("BrowserImeHandler::CreateImeWindow caret=" +
                  std::to_string(caret_w) + "x" +
                  std::to_string(caret_h));
  }
  if (system_caret_) {
    MoveImeWindow();
    ::ShowCaret(hwnd_);
  }
}

void BrowserImeHandler::DestroyImeWindow() {
  if (system_caret_) {
    DiagnosticLog("BrowserImeHandler::DestroyImeWindow");
    ::HideCaret(hwnd_);
    ::DestroyCaret();
    system_caret_ = false;
  }
}

void BrowserImeHandler::MoveImeWindow() {
  POINT caret_pos;
  caret_pos.x = caret_rect_.x;
  caret_pos.y = caret_rect_.y + caret_rect_.height;

  if (system_caret_) {
    ::SetCaretPos(caret_pos.x, caret_pos.y);
  }

  // Set candidate window position
  HIMC context = GetContext();
  if (context) {
    CANDIDATEFORM form;
    std::memset(&form, 0, sizeof(form));
    form.dwIndex = 0;
    form.dwStyle = CFS_CANDIDATEPOS;
    form.ptCurrentPos = caret_pos;

    const BOOL moved = ::ImmSetCandidateWindow(context, &form);
    DiagnosticLog("BrowserImeHandler::MoveImeWindow pos=" +
                  std::to_string(caret_pos.x) + "," +
                  std::to_string(caret_pos.y) +
                  " moved=" + (moved ? "true" : "false"));
    ReleaseContext(context);
  } else {
    DiagnosticLog("BrowserImeHandler::MoveImeWindow no HIMC");
  }
}

}  // namespace offscreen
