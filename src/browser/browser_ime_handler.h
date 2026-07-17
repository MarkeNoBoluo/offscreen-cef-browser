#pragma once

#include <vector>
#include <string>

#include <windows.h>
#include <imm.h>

#include "include/internal/cef_types_wrappers.h"

namespace offscreen {

class BrowserImeHandler {
 public:
  explicit BrowserImeHandler(HWND hwnd);
  ~BrowserImeHandler();

  BrowserImeHandler(const BrowserImeHandler&) = delete;
  BrowserImeHandler& operator=(const BrowserImeHandler&) = delete;

  void SetWindowHandle(HWND hwnd);
  void HandleImeSetContext(LPARAM& lParam);
  void HandleImeStartComposition();
  bool HandleImeComposition(LPARAM lParam,
                            std::wstring& commit_text,
                            std::wstring& composition_text,
                            std::vector<CefCompositionUnderline>& underlines,
                            CefRange& selection_range);
  void HandleImeEndComposition();
  void HandleInputLanguageChange();
  void UpdateCompositionRange(const CefRange& selection_range,
                              const std::vector<CefRect>& character_bounds);
  void UpdateCaretPosition(int composition_index);
  void ResetComposition();
  void CancelComposition();
  bool is_composing() const { return is_composing_; }

 private:
  void CreateImeWindow();
  void DestroyImeWindow();
  void MoveImeWindow();
  HIMC GetContext();
  void ReleaseContext(HIMC context);

  HWND hwnd_;
  bool is_composing_ = false;
  bool system_caret_ = false;
  CefRect caret_rect_;
  LANGID input_language_id_ = 0;
};

}  // namespace offscreen
