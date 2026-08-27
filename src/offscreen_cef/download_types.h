#pragma once

#include <cstdint>

namespace offscreen {

using DownloadRequestId = std::uint32_t;

enum class DownloadDecisionMode {
  kAutomatic,
  kAskHost,
};

}  // namespace offscreen
