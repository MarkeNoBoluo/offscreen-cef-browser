#include "app/app_config.h"

#include <string>

namespace offscreen {
namespace {

// 命令行中用于覆盖首个页面地址的参数前缀。
constexpr char kUrlPrefix[] = "--url=";

/// 判断字符串是否以给定前缀开头。
/// @param value 待检查文本。
/// @param prefix 所需前缀。
/// @return 匹配时为 true。
bool StartsWith(const std::string& value, const std::string& prefix) {
  return value.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

/// 从 --url= 参数提取首个标签页地址。
/// @param argc 参数个数。
/// @param argv 参数数组。
/// @return 解析后的启动配置。
AppConfig AppConfig::FromArgs(int argc, const char* const argv[]) {
  AppConfig config;

  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i] ? argv[i] : "";
    if (!StartsWith(argument, kUrlPrefix)) {
      continue;
    }

    const std::string url = argument.substr(std::string(kUrlPrefix).size());
    if (!url.empty()) {
      config.initial_url = url;
    }
  }

  return config;
}

}  // namespace offscreen
