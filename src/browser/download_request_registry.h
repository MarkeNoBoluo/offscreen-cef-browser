#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>

#include "offscreen_cef/download_types.h"

namespace offscreen {

class DownloadRequestRegistry final {
 public:
  using ContinueCallback = std::function<void(const std::wstring&)>;

  bool Add(DownloadRequestId id, ContinueCallback callback);
  bool Accept(DownloadRequestId id, const std::wstring& full_path);
  bool Reject(DownloadRequestId id);
  void RejectAll();
  std::size_t pending_count() const;

 private:
  std::unordered_map<DownloadRequestId, ContinueCallback> pending_;
};

}  // namespace offscreen
