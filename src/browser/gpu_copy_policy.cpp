#include "browser/gpu_copy_policy.h"

#include <chrono>

namespace offscreen {

namespace {

int64_t SteadyNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

GpuCopyPolicy::GpuCopyPolicy() : clock_(SteadyNowMs) {}

GpuCopyPolicy::GpuCopyPolicy(Clock clock) : clock_(std::move(clock)) {}

bool GpuCopyPolicy::CopyEnabled(GpuFrameKind kind) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const int64_t now = clock_();
  if (device_failed_) {
    if (now < device_retry_deadline_ms_) {
      return false;
    }
    // 设备退避到期：允许重试打开设备。
    return true;
  }
  const ResourceState& state = Select(kind);
  if (!state.copy_enabled) {
    return now >= state.retry_deadline_ms;
  }
  return true;
}

bool GpuCopyPolicy::DeviceFailed() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return device_failed_;
}

void GpuCopyPolicy::ReportPresentOutcome(GpuFrameKind kind, bool gpu_used,
                                         const std::string& failure_stage) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (gpu_used) {
    // 成功呈现：清除该资源与设备失败态。
    ResourceState& state = Select(kind);
    state.copy_enabled = true;
    state.consecutive_lock_failures = 0;
    device_failed_ = false;
    return;
  }

  if (failure_stage == "open_device") {
    device_failed_ = true;
    device_retry_deadline_ms_ = clock_() + kRetryBackoffMs;
    return;
  }
  if (failure_stage == "register" || failure_stage == "lock" ||
      failure_stage == "unlock") {
    DisableResource(kind);
  }
}

bool GpuCopyPolicy::RetryDue(GpuFrameKind kind) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !Select(kind).copy_enabled &&
         clock_() >= Select(kind).retry_deadline_ms && !device_failed_;
}

void GpuCopyPolicy::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (ResourceState& state : resources_) {
    state = ResourceState{};
  }
  device_failed_ = false;
  device_retry_deadline_ms_ = 0;
}

void GpuCopyPolicy::ResetDeviceRetry() {
  std::lock_guard<std::mutex> lock(mutex_);
  device_failed_ = false;
  device_retry_deadline_ms_ = 0;
}

GpuCopyPolicy::ResourceState& GpuCopyPolicy::Select(GpuFrameKind kind) {
  const std::size_t index =
      kind == GpuFrameKind::kPopup ? 1u : 0u;
  return resources_[index];
}

const GpuCopyPolicy::ResourceState& GpuCopyPolicy::Select(
    GpuFrameKind kind) const {
  const std::size_t index =
      kind == GpuFrameKind::kPopup ? 1u : 0u;
  return resources_[index];
}

void GpuCopyPolicy::DisableResource(GpuFrameKind kind) {
  ResourceState& state = Select(kind);
  state.copy_enabled = false;
  state.retry_deadline_ms = clock_() + kRetryBackoffMs;
  ++state.consecutive_lock_failures;
}

}  // namespace offscreen
