#pragma once

#include <memory>
#include <optional>
#include <string>

#include <windows.h>

namespace offscreen {

class CefWebView;

struct CefRuntimeOptions {
  std::wstring subprocess_path;
  std::wstring cache_path;
  std::wstring log_path;
};

// Process-scoped CEF integration. Create exactly one instance in the host Qt
// application and shut it down only after every CefWebView has closed.
class CefRuntime final {
 public:
  CefRuntime();
  ~CefRuntime();

  CefRuntime(const CefRuntime&) = delete;
  CefRuntime& operator=(const CefRuntime&) = delete;

  // Call before constructing QApplication. A value means this process is a
  // CEF subprocess and must return that exit code immediately.
  static std::optional<int> ExecuteSubprocess(HINSTANCE instance);

  bool Initialize(const CefRuntimeOptions& options = {});
  bool Shutdown();
  bool IsInitialized() const;
  bool AllBrowsersClosed() const;
  void CloseAllBrowsers();

  static CefRuntime* Active();

 private:
  friend class CefWebView;

  void RegisterWebView(CefWebView* view);
  void UnregisterWebView(CefWebView* view);

  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace offscreen
