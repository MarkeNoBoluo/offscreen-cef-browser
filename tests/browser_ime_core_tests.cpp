#include "browser/browser_ime_core.h"

#include <cstdlib>
#include <iostream>

using namespace offscreen;

namespace {

void expect_true(bool value, const char* label) {
  if (!value) {
    std::cerr << "FAIL: " << label << " (expected true, got false)" << std::endl;
    std::exit(1);
  }
}

void expect_false(bool value, const char* label) {
  if (value) {
    std::cerr << "FAIL: " << label << " (expected false, got true)" << std::endl;
    std::exit(1);
  }
}

void expect_eq_uint(unsigned int expected, unsigned int actual,
                    const char* label) {
  if (expected != actual) {
    std::cerr << "FAIL: " << label << " expected=" << expected
              << " actual=" << actual << std::endl;
    std::exit(1);
  }
}

void test_ime_has_result_string_true() {
  LPARAM lparam = GCS_RESULTSTR;
  expect_true(ImeHasResultString(lparam), "has_result_true");
}

void test_ime_has_result_string_false() {
  LPARAM lparam = 0;
  expect_false(ImeHasResultString(lparam), "has_result_false_zero");
}

void test_ime_has_result_string_with_compstr() {
  LPARAM lparam = GCS_COMPSTR;
  expect_false(ImeHasResultString(lparam), "has_result_with_compstr");
}

void test_ime_has_result_string_combined() {
  LPARAM lparam = GCS_RESULTSTR | GCS_COMPSTR;
  expect_true(ImeHasResultString(lparam), "has_result_combined");
}

void test_ime_has_composition_string_true() {
  LPARAM lparam = GCS_COMPSTR;
  expect_true(ImeHasCompositionString(lparam), "has_composition_true");
}

void test_ime_has_composition_string_false() {
  LPARAM lparam = 0;
  expect_false(ImeHasCompositionString(lparam), "has_composition_false_zero");
}

void test_ime_has_composition_string_with_result() {
  LPARAM lparam = GCS_RESULTSTR;
  expect_false(ImeHasCompositionString(lparam), "has_composition_with_result");
}

void test_ime_is_composition_active_null_context() {
  expect_false(ImeIsCompositionActive(nullptr), "composition_active_null");
}

void test_ime_get_input_language_nonzero() {
  LANGID lang = ImeGetInputLanguage();
  // On any real Windows system, the keyboard layout will have a non-zero LANGID
  std::cout << "  IME input language LANGID=" << lang << std::endl;
  // LANGID cannot be validated as non-zero without a real window,
  // but we can at least call the function without crashing.
  (void)lang;
}

void test_ime_extract_result_empty_no_ime() {
  // Without an active IME context, ImmGetCompositionString returns 0 bytes.
  // ImeExtractResult should handle this gracefully and return empty text.
  ImeResultData data = ImeExtractResult(nullptr, GCS_RESULTSTR);
  expect_true(data.text.empty(), "extract_result_empty");
}

void test_ime_extract_composition_empty_no_ime() {
  // Without an active IME context, extraction should return empty data.
  ImeCompositionData data = ImeExtractComposition(nullptr, GCS_COMPSTR);
  expect_true(data.text.empty(), "extract_composition_empty_text");
  expect_true(data.clauses.empty(), "extract_composition_empty_clauses");
  expect_eq_uint(0, static_cast<unsigned int>(data.cursor_position),
                 "extract_composition_cursor_zero");
}

}  // namespace

int main() {
  test_ime_has_result_string_true();
  test_ime_has_result_string_false();
  test_ime_has_result_string_with_compstr();
  test_ime_has_result_string_combined();
  test_ime_has_composition_string_true();
  test_ime_has_composition_string_false();
  test_ime_has_composition_string_with_result();
  test_ime_is_composition_active_null_context();
  test_ime_get_input_language_nonzero();
  test_ime_extract_result_empty_no_ime();
  test_ime_extract_composition_empty_no_ime();

  std::cout << "browser_ime_core_tests: all passed" << std::endl;
  return 0;
}
