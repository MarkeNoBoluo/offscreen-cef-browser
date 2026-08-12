#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "browser/gpu_frame_state.h"

namespace offscreen {

// GPU 复制降级状态机（纯 STL，无 D3D/GL 依赖，时钟可注入供单测）。
//
// 用途：当 GPU 呈现链路（WGL_DX_interop + D3D11 共享纹理）在某类资源上
// 持续失败时，按资源或按设备降级，并用时间退避代替代次变化触发重试。
// 禁用后无新 GPU 帧、代次不变化，无法用代次触发重试，因此改用时间退避：
// 到期的下一次 OnAcceleratedPaint 允许一次性重试复制，同时做 CPU readback
// 保证可见性；GL 成功则回正常态，否则再降级。
class GpuCopyPolicy {
 public:
  // 时钟回调：返回 steady_clock 单调毫秒；测试注入 fake clock。
  using Clock = std::function<int64_t()>;

  // 默认退避间隔（毫秒）：资源级或设备级失败后等待再重试。
  static constexpr int64_t kRetryBackoffMs = 5000;

  GpuCopyPolicy();
  explicit GpuCopyPolicy(Clock clock);

  // 该资源当前是否允许发起 GPU 复制。设备级失败或资源级退避未到期时为
  // false；退避到期后返回 true（允许一次性重试）。
  bool CopyEnabled(GpuFrameKind kind) const;

  // 设备级失败是否生效（interop 整体不可用）。
  bool DeviceFailed() const;

  // 报告一次 GPU 呈现结果，用于更新降级状态。
  // @param kind 主视图或弹出层。
  // @param gpu_used 是否真正走了 GPU 呈现（true 表示成功，清除失败态）。
  // @param failure_stage 失败阶段：""（无失败）、"register"、"lock"、
  //                      "unlock"（资源级）、"open_device"（设备级）。
  void ReportPresentOutcome(GpuFrameKind kind, bool gpu_used,
                            const std::string& failure_stage);

  // 时间退避查询：该资源当前是否已到重试时刻（仅当 copy 被禁用时有意义）。
  bool RetryDue(GpuFrameKind kind) const;

  // 清除全部失败态（resize / 用户复位口）。
  void Reset();

  // 仅清除设备级失败态，使下一次 CopyEnabled 允许重新打开设备。
  void ResetDeviceRetry();

 private:
  struct ResourceState {
    bool copy_enabled = true;
    int64_t retry_deadline_ms = 0;
    uint32_t consecutive_lock_failures = 0;
  };

  ResourceState& Select(GpuFrameKind kind);
  const ResourceState& Select(GpuFrameKind kind) const;
  void DisableResource(GpuFrameKind kind);

  Clock clock_;
  std::array<ResourceState, 2> resources_{};  // [kView, kPopup]
  bool device_failed_ = false;
  int64_t device_retry_deadline_ms_ = 0;
  // CEF 线程（bridge 复制上报）与 Qt 线程（widget 呈现反馈）并发访问，
  // 全部公共方法必须在持有此锁的情况下读写状态。
  mutable std::mutex mutex_;
};

}  // namespace offscreen
