#include "browser/download_request_registry.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expect_true(bool value, const char* label) {
  if (!value) {
    std::cerr << label << " expected true\n";
    std::exit(1);
  }
}

void expect_false(bool value, const char* label) {
  if (value) {
    std::cerr << label << " expected false\n";
    std::exit(1);
  }
}

void test_accepts_each_request_once() {
  offscreen::DownloadRequestRegistry registry;
  int continue_count = 0;
  std::wstring accepted_path;
  expect_true(registry.Add(7, [&](const std::wstring& path) {
    ++continue_count;
    accepted_path = path;
  }), "add request");
  expect_true(registry.Accept(7, L"C:\\Downloads\\report.pdf"),
              "accept request");
  expect_false(registry.Accept(7, L"C:\\Downloads\\second.pdf"),
               "second accept");
  expect_true(continue_count == 1, "continue once");
  expect_true(accepted_path == L"C:\\Downloads\\report.pdf",
              "selected path forwarded");
}

void test_rejects_without_continuing() {
  offscreen::DownloadRequestRegistry registry;
  int continue_count = 0;
  registry.Add(8, [&](const std::wstring&) { ++continue_count; });
  expect_true(registry.Reject(8), "reject request");
  expect_false(registry.Reject(8), "second reject");
  expect_true(continue_count == 0, "reject does not continue");
}

void test_tracks_concurrent_requests_independently() {
  offscreen::DownloadRequestRegistry registry;
  int first = 0;
  int second = 0;
  registry.Add(10, [&](const std::wstring&) { ++first; });
  registry.Add(11, [&](const std::wstring&) { ++second; });
  expect_true(registry.Accept(11, L"C:\\Downloads\\b.txt"),
              "accept second");
  expect_true(registry.Reject(10), "reject first");
  expect_true(first == 0 && second == 1, "independent decisions");
  expect_true(registry.pending_count() == 0, "no pending requests");
}

void test_reject_all_releases_pending_requests() {
  offscreen::DownloadRequestRegistry registry;
  registry.Add(20, [](const std::wstring&) {});
  registry.Add(21, [](const std::wstring&) {});
  registry.RejectAll();
  expect_true(registry.pending_count() == 0, "reject all clears requests");
}

void test_rejects_empty_callback() {
  offscreen::DownloadRequestRegistry registry;
  expect_false(registry.Add(30, {}), "add empty callback");
  expect_true(registry.pending_count() == 0, "empty callback not stored");
}

void test_duplicate_add_preserves_original_callback() {
  offscreen::DownloadRequestRegistry registry;
  int original_count = 0;
  int duplicate_count = 0;
  expect_true(registry.Add(40, [&](const std::wstring&) { ++original_count; }),
              "add original request");
  expect_false(registry.Add(40, [&](const std::wstring&) { ++duplicate_count; }),
               "add duplicate request");
  expect_true(registry.Accept(40, L"C:\\Downloads\\original.txt"),
              "accept original request");
  expect_true(original_count == 1, "original callback retained");
  expect_true(duplicate_count == 0, "duplicate callback not invoked");
}

void test_accept_removes_request_before_callback_reentry() {
  offscreen::DownloadRequestRegistry registry;
  bool nested_accept_result = true;
  expect_true(registry.Add(50, [&](const std::wstring&) {
                nested_accept_result =
                    registry.Accept(50, L"C:\\Downloads\\nested.txt");
              }),
              "add reentrant request");
  expect_true(registry.Accept(50, L"C:\\Downloads\\outer.txt"),
              "accept outer request");
  expect_false(nested_accept_result, "reentrant accept");
  expect_true(registry.pending_count() == 0, "reentrant request removed");
}

}  // namespace

int main() {
  test_accepts_each_request_once();
  test_rejects_without_continuing();
  test_tracks_concurrent_requests_independently();
  test_reject_all_releases_pending_requests();
  test_rejects_empty_callback();
  test_duplicate_add_preserves_original_callback();
  test_accept_removes_request_before_callback_reentry();
  return 0;
}
