#include "app/browser_window_title.h"

namespace offscreen {

/// 组合固定应用名与 CEF 创建失败详情。
/// @param error_detail 失败原因。
/// @return 可直接显示的窗口标题。
std::string BrowserCreationFailedTitle(const std::string& error_detail) {
  return "Offscreen CEF Browser - Browser creation failed: " + error_detail;
}

}  // namespace offscreen
