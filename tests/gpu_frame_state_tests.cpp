#include "browser/gpu_frame_state.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

template <typename A, typename B>
void expect_eq(A actual, B expected, const char* label) {
  if (actual != expected) {
    std::cerr << label << " expected [" << expected << "] but got [" << actual
              << "]\n";
    std::exit(1);
  }
}

void test_same_resource_increments_only_frame_generation() {
  offscreen::GpuFramePublicationState state;

  const auto first = state.Publish(offscreen::GpuFrameKind::kView, 1920, 1080,
                                   87);
  const auto second = state.Publish(offscreen::GpuFrameKind::kView, 1920, 1080,
                                    87);

  expect_eq(first.resource_generation, uint64_t{1}, "first resource");
  expect_eq(first.frame_generation, uint64_t{1}, "first frame");
  expect_eq(second.resource_generation, uint64_t{1}, "same resource");
  expect_eq(second.frame_generation, uint64_t{2}, "second frame");
}

void test_size_or_format_change_increments_resource_generation() {
  offscreen::GpuFramePublicationState state;

  state.Publish(offscreen::GpuFrameKind::kView, 1920, 1080, 87);
  const auto resized =
      state.Publish(offscreen::GpuFrameKind::kView, 1280, 720, 87);
  const auto reformatted =
      state.Publish(offscreen::GpuFrameKind::kView, 1280, 720, 88);

  expect_eq(resized.resource_generation, uint64_t{2}, "resized resource");
  expect_eq(reformatted.resource_generation, uint64_t{3},
            "reformatted resource");
}

void test_view_and_popup_generations_are_independent() {
  offscreen::GpuFramePublicationState state;

  const auto view =
      state.Publish(offscreen::GpuFrameKind::kView, 800, 600, 87);
  const auto popup =
      state.Publish(offscreen::GpuFrameKind::kPopup, 200, 100, 87);

  expect_eq(view.resource_generation, uint64_t{1}, "view resource");
  expect_eq(view.frame_generation, uint64_t{1}, "view frame");
  expect_eq(popup.resource_generation, uint64_t{1}, "popup resource");
  expect_eq(popup.frame_generation, uint64_t{1}, "popup frame");
}

}  // namespace

int main() {
  test_same_resource_increments_only_frame_generation();
  test_size_or_format_change_increments_resource_generation();
  test_view_and_popup_generations_are_independent();
  return 0;
}
