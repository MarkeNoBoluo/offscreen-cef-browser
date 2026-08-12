#include "browser/gpu_copy_policy.h"

#include <cstdlib>
#include <iostream>

namespace {

using offscreen::GpuCopyPolicy;
using offscreen::GpuFrameKind;

int64_t g_now_ms = 0;
int64_t FakeClock() {
  return g_now_ms;
}

template <typename A, typename B>
void expect_eq(A actual, B expected, const char* label) {
  if (actual != expected) {
    std::cerr << label << " expected [" << expected << "] but got [" << actual
              << "]\n";
    std::exit(1);
  }
}

void expect_true(bool actual, const char* label) {
  if (!actual) {
    std::cerr << label << " expected true but got false\n";
    std::exit(1);
  }
}

void test_initial_state_enables_copy() {
  g_now_ms = 0;
  GpuCopyPolicy policy(FakeClock);
  expect_true(policy.CopyEnabled(GpuFrameKind::kView), "view copy initially enabled");
  expect_true(policy.CopyEnabled(GpuFrameKind::kPopup), "popup copy initially enabled");
  expect_true(!policy.DeviceFailed(), "device not failed initially");
}

void test_resource_failure_disables_only_that_kind() {
  g_now_ms = 0;
  GpuCopyPolicy policy(FakeClock);
  policy.ReportPresentOutcome(GpuFrameKind::kView, /*gpu_used=*/false, "lock");
  expect_true(!policy.CopyEnabled(GpuFrameKind::kView), "view copy disabled after lock failure");
  expect_true(policy.CopyEnabled(GpuFrameKind::kPopup), "popup copy unaffected");
  expect_true(!policy.DeviceFailed(), "device not failed on lock stage");
  expect_true(!policy.CopyEnabled(GpuFrameKind::kView),
              "view still disabled before backoff elapses");
}

void test_resource_retry_after_backoff() {
  g_now_ms = 0;
  GpuCopyPolicy policy(FakeClock);
  policy.ReportPresentOutcome(GpuFrameKind::kView, /*gpu_used=*/false, "register");
  g_now_ms = GpuCopyPolicy::kRetryBackoffMs;
  expect_true(policy.CopyEnabled(GpuFrameKind::kView),
              "view copy re-enabled after backoff (one-shot retry)");
}

void test_device_failure_disables_everything_until_retry() {
  g_now_ms = 0;
  GpuCopyPolicy policy(FakeClock);
  policy.ReportPresentOutcome(GpuFrameKind::kView, /*gpu_used=*/false, "open_device");
  expect_true(policy.DeviceFailed(), "device failed after open_device");
  expect_true(!policy.CopyEnabled(GpuFrameKind::kView), "view blocked by device failure");
  expect_true(!policy.CopyEnabled(GpuFrameKind::kPopup), "popup blocked by device failure");
  g_now_ms = GpuCopyPolicy::kRetryBackoffMs;
  expect_true(policy.CopyEnabled(GpuFrameKind::kView),
              "device retry allowed after backoff");
  expect_true(policy.DeviceFailed(), "device still marked failed before successful present");
}

void test_success_clears_resource_and_device_failure() {
  g_now_ms = 0;
  GpuCopyPolicy policy(FakeClock);
  policy.ReportPresentOutcome(GpuFrameKind::kView, /*gpu_used=*/false, "lock");
  policy.ReportPresentOutcome(GpuFrameKind::kPopup, /*gpu_used=*/false, "open_device");
  expect_true(policy.DeviceFailed(), "device failed");
  policy.ReportPresentOutcome(GpuFrameKind::kView, /*gpu_used=*/true, "");
  expect_true(policy.CopyEnabled(GpuFrameKind::kView), "view re-enabled on success");
  expect_true(!policy.DeviceFailed(), "device re-enabled on successful present");
}

void test_unknown_stage_is_ignored() {
  g_now_ms = 0;
  GpuCopyPolicy policy(FakeClock);
  policy.ReportPresentOutcome(GpuFrameKind::kView, /*gpu_used=*/false, "bogus");
  expect_true(policy.CopyEnabled(GpuFrameKind::kView), "unknown stage does not disable");
}

void test_reset_clears_all_state() {
  g_now_ms = 0;
  GpuCopyPolicy policy(FakeClock);
  policy.ReportPresentOutcome(GpuFrameKind::kView, /*gpu_used=*/false, "lock");
  policy.ReportPresentOutcome(GpuFrameKind::kPopup, /*gpu_used=*/false, "open_device");
  policy.Reset();
  expect_true(policy.CopyEnabled(GpuFrameKind::kView), "view re-enabled after reset");
  expect_true(policy.CopyEnabled(GpuFrameKind::kPopup), "popup re-enabled after reset");
  expect_true(!policy.DeviceFailed(), "device re-enabled after reset");
}

void test_retry_due_only_after_backoff_when_disabled() {
  g_now_ms = 0;
  GpuCopyPolicy policy(FakeClock);
  expect_true(!policy.RetryDue(GpuFrameKind::kView),
              "no retry due while copy enabled");
  policy.ReportPresentOutcome(GpuFrameKind::kView, /*gpu_used=*/false, "lock");
  expect_true(!policy.RetryDue(GpuFrameKind::kView),
              "not due before backoff elapses");
  g_now_ms = GpuCopyPolicy::kRetryBackoffMs;
  expect_true(policy.RetryDue(GpuFrameKind::kView), "retry due after backoff");
}

}  // namespace

int main() {
  g_now_ms = 0;
  test_initial_state_enables_copy();
  test_resource_failure_disables_only_that_kind();
  test_resource_retry_after_backoff();
  test_device_failure_disables_everything_until_retry();
  test_success_clears_resource_and_device_failure();
  test_unknown_stage_is_ignored();
  test_reset_clears_all_state();
  test_retry_due_only_after_backoff_when_disabled();
  std::cout << "gpu_copy_policy_tests passed\n";
  return 0;
}
