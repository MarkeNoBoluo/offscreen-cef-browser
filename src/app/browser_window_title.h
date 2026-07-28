#pragma once

#include <string>

namespace offscreen {

/// 生成浏览器创建失败时显示的窗口标题。
/// @param error_detail CEF 返回或记录的失败详情。
/// @return 包含失败详情的 UTF-8 标题文本。
std::string BrowserCreationFailedTitle(const std::string& error_detail);

}  // namespace offscreen
