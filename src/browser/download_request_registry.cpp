#include "browser/download_request_registry.h"

#include <utility>

namespace offscreen {

bool DownloadRequestRegistry::Add(DownloadRequestId id,
                                  ContinueCallback callback) {
  if (!callback || pending_.find(id) != pending_.end()) {
    return false;
  }
  pending_.emplace(id, std::move(callback));
  return true;
}

bool DownloadRequestRegistry::Accept(DownloadRequestId id,
                                     const std::wstring& full_path) {
  auto it = pending_.find(id);
  if (it == pending_.end()) {
    return false;
  }
  ContinueCallback callback = std::move(it->second);
  pending_.erase(it);
  callback(full_path);
  return true;
}

bool DownloadRequestRegistry::Reject(DownloadRequestId id) {
  return pending_.erase(id) == 1;
}

void DownloadRequestRegistry::RejectAll() {
  pending_.clear();
}

std::size_t DownloadRequestRegistry::pending_count() const {
  return pending_.size();
}

}  // namespace offscreen
